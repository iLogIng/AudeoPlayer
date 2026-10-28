#include "output/AudioOutput.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

namespace
{

// 环形缓冲区最多缓存约 250ms 音频数据
constexpr size_t kBufferDurationMs = 250;
// 预缓冲约 100ms 后开始播放
constexpr size_t kPrebufferDurationMs = 100;
// SDL 回调中使用的栈缓冲大小
constexpr size_t kCallbackBufferBytes = 4096;
// 排空时额外增加的余量
constexpr uint32_t kDrainMarginMs = 20;

} // namespace

AudioOutput::~AudioOutput()
{
    close();
}

void AudioOutput::setStartedCallback(StartedCallback callback)
{
    std::lock_guard lock(m_startedMutex);
    m_startedCallback = std::move(callback);
}

// 通知音频开始
void AudioOutput::notifyStarted()
{
    StartedCallback callback;
    {
        std::lock_guard lock(m_startedMutex);
        callback = m_startedCallback;
    }

    if (!callback) return;

    try {
        callback();
    } catch (...) {
        // 回调异常不能影响音频线程
    }
}

// 恢复设备并通知主时钟开始
bool AudioOutput::resumeDevice()
{
    if (!m_stream || m_closing.load()) return false;

    // 先恢复时钟再启动设备 避免音频回调在时钟暂停时发布新锚点
    const bool wasPaused = m_clock.paused();
    m_clock.resume();

    // 恢复SDL音频流设备
    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        logging::ERROR_LOG("AudioOutput::resumeDevice", SDL_GetError());
        if (wasPaused)
            m_clock.pause();
        return false;
    }

    m_deviceResumed.store(true);

    // 重新开始音频
    if (wasPaused)
        notifyStarted();

    return true;
}

// 打开音频输出设备
void AudioOutput::open()
{
    close();

    // SDL 音频设备描述符
    SDL_AudioSpec deviceSpec{};
    int sampleFrames = 0;
    // 音频设备格式
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                  &deviceSpec, &sampleFrames)) {
        logging::FAILED_LOG("AudioOutput::open", SDL_GetError());
        throw std::runtime_error("audio output: can not get device format");
    }
    m_spec = deviceSpec;

    // 采样点大小
    const size_t bytesPerSample = SDL_AUDIO_BYTESIZE(m_spec.format);
    if (m_spec.freq <= 0 || m_spec.channels <= 0 || bytesPerSample == 0) {
        logging::FAILED_LOG("AudioOutput::open", "invalid audio device format");
        throw std::runtime_error("audio output: invalid audio device format");
    }

    // 帧字节数
    m_frameBytes = static_cast<size_t>(m_spec.channels) * bytesPerSample;
    // 每秒字节数
    m_bytesPerSecond = static_cast<size_t>(m_spec.freq) * m_frameBytes;
    // 设备可缓冲时间
    m_deviceBufferDurationMs = sampleFrames > 0
        ? static_cast<uint32_t>(
              static_cast<uint64_t>(sampleFrames) * 1000 / m_spec.freq)
        : 0;

    // 创建环形缓冲区
    const size_t capacityBytes = std::max<size_t>(
        m_bytesPerSecond * kBufferDurationMs / 1000, 1);
    m_prebufferBytes = std::max<size_t>(
        m_bytesPerSecond * kPrebufferDurationMs / 1000, 1);
    m_ring = std::make_unique<PcmRingBuffer>(capacityBytes);

    // 初始化音频时钟
    m_clock.reset(m_spec.freq);
    m_clock.setDeviceLatencyUs(static_cast<int64_t>(m_deviceBufferDurationMs) * 1000);
    m_basePtsSet = false;
    m_endOfStream.store(false);

    // 使用回调模式打开音频设备流 设备默认处于暂停状态
    m_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &m_spec,
        &AudioOutput::streamCallback, this);
    if (!m_stream) {
        logging::FAILED_LOG("AudioOutput::open", SDL_GetError());
        m_ring.reset();
        m_prebufferBytes = 0;
        m_bytesPerSecond = 0;
        m_frameBytes = 0;
        m_deviceBufferDurationMs = 0;
        m_clock.reset(0);
        throw std::runtime_error("audio output: can not open audio stream");
    }
}

// 写入 PCM 数据
bool AudioOutput::push(const uint8_t *pcmData, size_t bytes, int64_t ptsUs,
                       std::stop_token st)
{
    if (m_closing.load() || !m_ring) return false;

    // 第一段数据用于建立音频时钟基准
    if (!m_basePtsSet) {
        m_clock.setBasePtsUs(ptsUs);
        m_basePtsSet = true;
    }

    // 写入环形缓冲
    if (!m_ring->write(pcmData, bytes, st)) return false;

    // 达到预缓冲后自动开始播放
    maybeResume();
    return true;
}

// 标记所有 PCM 已写入
void AudioOutput::markEndOfStream()
{
    m_endOfStream.store(true);
}

// 文件结束时强制恢复设备
void AudioOutput::resume()
{
    if (m_closing.load() || m_paused.load() || m_deviceResumed.load()) return;
    resumeDevice();
}

// 队列字节长度 包含环形缓冲区和 SDL 内部缓冲
size_t AudioOutput::queuedBytes() const
{
    size_t queued = m_ring ? m_ring->availableRead() : 0;
    if (m_stream) {
        const int sdlQueued = SDL_GetAudioStreamQueued(m_stream);
        if (sdlQueued > 0)
            queued += static_cast<size_t>(sdlQueued);
    }
    return queued;
}

// 估算剩余播放时长
uint32_t AudioOutput::drainDurationMs() const
{
    if (m_bytesPerSecond == 0) return 0;

    // 队列中缓冲的时长
    const uint64_t queuedMs =
        static_cast<uint64_t>(queuedBytes()) * 1000 / m_bytesPerSecond;
    return static_cast<uint32_t>(queuedMs + m_deviceBufferDurationMs + kDrainMarginMs);
}

// 暂停音频流输出设备
void AudioOutput::pause(bool paused)
{
    if (!m_stream) return;
    m_paused.store(paused);

    if (paused) {
        m_clock.pause();
        SDL_PauseAudioStreamDevice(m_stream);
        return;
    }

    if (m_deviceResumed.load()) {
        resumeDevice();
    } else {
        maybeResume();
    }
}

// 设置音量
void AudioOutput::setVolume(float volume)
{
    if (!m_stream) return;
    if (!SDL_SetAudioStreamGain(m_stream, volume)) {
        logging::ERROR_LOG("AudioOutput::setVolume", SDL_GetError());
    }
}

// 唤醒可能阻塞在 ring 写入的生产者
void AudioOutput::abort()
{
    if (m_ring) m_ring->abort();
}

// 关闭音频输出设备
void AudioOutput::close()
{
    m_closing.store(true);

    // 唤醒可能阻塞在 write 的生产者
    abort();

    // 先停止并销毁 SDL 流 确保回调不再运行
    if (m_stream) {
        SDL_PauseAudioStreamDevice(m_stream);
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }

    // 最后释放环形缓冲区
    m_ring.reset();
    m_prebufferBytes = 0;
    m_bytesPerSecond = 0;
    m_frameBytes = 0;
    m_deviceBufferDurationMs = 0;
    m_basePtsSet = false;
    m_clock.reset(0);
    m_deviceResumed.store(false);
    m_paused.store(false);
    m_endOfStream.store(false);
    m_closing.store(false);

    {
        std::lock_guard lock(m_startedMutex);
        m_startedCallback = nullptr;
    }

    // 仅在出现欠载时输出诊断信息 读取后清零避免重复输出
    const uint64_t underruns = m_underruns.exchange(0, std::memory_order_relaxed);
    if (underruns > 0) {
        char message[64];
        std::snprintf(message, sizeof(message), "audio underruns: %llu",
                      static_cast<unsigned long long>(underruns));
        logging::INFO_LOG("AudioOutput::close", message);
    }
}

// 达到预缓冲后恢复设备
void AudioOutput::maybeResume()
{
    if (m_closing.load() || m_paused.load() || m_deviceResumed.load()) return;
    if (!m_ring || m_ring->availableRead() < m_prebufferBytes) return;

    resumeDevice();
}

// SDL 音频回调 从环形缓冲区读取数据并推入 SDL 流
void SDLCALL AudioOutput::streamCallback(void *userdata, SDL_AudioStream *stream,
                                         int additionalAmount, int totalAmount)
{
    (void)totalAmount;

    auto *self = static_cast<AudioOutput *>(userdata);
    if (!self || self->m_closing.load() || self->m_paused.load()) return;
    if (additionalAmount <= 0 || !self->m_ring) return;

    uint8_t buffer[kCallbackBufferBytes];
    size_t remaining = static_cast<size_t>(additionalAmount);
    // 已送入设备的字节数
    size_t fedBytes = 0;

    while (remaining > 0) {
        const size_t want = std::min(remaining, sizeof(buffer));
        const size_t n = self->m_ring->read(buffer, want);
        if (n == 0) {
            // 所有 PCM 已送入后 尾部静音补齐不算欠载
            if (!self->m_endOfStream.load(std::memory_order_relaxed))
                self->m_underruns.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        if (!SDL_PutAudioStreamData(stream, buffer, static_cast<int>(n)))
            break;

        fedBytes += n;
        remaining -= n;
    }

    // 音频结束后不再重锚时钟 让播放时间线继续按墙钟前进
    if (self->m_endOfStream.load(std::memory_order_relaxed) && fedBytes == 0)
        return;

    // 更新音频时钟
    int64_t frames = 0;
    if (self->m_frameBytes > 0 && fedBytes > 0) {
        // 已送入 SDL 的帧数
        frames = static_cast<int64_t>(fedBytes / self->m_frameBytes);
    }

    // 仍在 SDL 音频设备中排队的数据
    int64_t bufferedUs = 0;
    if (self->m_bytesPerSecond > 0) {
        int sdlQueued = SDL_GetAudioStreamQueued(stream);
        if (sdlQueued < 0) sdlQueued = 0;
        bufferedUs = static_cast<int64_t>(sdlQueued) * 1'000'000
                   / static_cast<int64_t>(self->m_bytesPerSecond);
    }

    self->m_clock.advance(frames, bufferedUs);
}

} // namespace avplayer
