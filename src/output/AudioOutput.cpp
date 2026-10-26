#include "output/AudioOutput.hpp"

#include <algorithm>
#include <cstdio>
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

} // namespace

AudioOutput::~AudioOutput()
{
    close();
}

// 打开音频输出设备
void AudioOutput::open()
{
    close();

    // SDL 音频设备描述符
    SDL_AudioSpec deviceSpec{};
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &deviceSpec, nullptr)) {
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

    // 每秒字节数
    const size_t bytesPerSecond = static_cast<size_t>(m_spec.freq)
                                * static_cast<size_t>(m_spec.channels)
                                * bytesPerSample;

    // 创建环形缓冲区
    const size_t capacityBytes = std::max<size_t>(
        bytesPerSecond * kBufferDurationMs / 1000, 1);
    // 预缓冲区大小
    m_prebufferBytes = std::max<size_t>(
        bytesPerSecond * kPrebufferDurationMs / 1000, 1);
    m_ring = std::make_unique<PcmRingBuffer>(capacityBytes);

    // 使用回调模式打开音频设备流 设备默认处于暂停状态
    m_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &m_spec,
        &AudioOutput::streamCallback, this);
    if (!m_stream) {
        logging::FAILED_LOG("AudioOutput::open", SDL_GetError());
        m_ring.reset();
        m_prebufferBytes = 0;
        throw std::runtime_error("audio output: can not open audio stream");
    }
}

// 写入 PCM 数据
bool AudioOutput::push(const uint8_t *pcmData, size_t bytes, std::stop_token st)
{
    if (m_closing.load() || !m_ring) return false;
    if (!m_ring->write(pcmData, bytes, st)) return false;

    // 达到预缓冲后自动开始播放
    maybeResume();
    return true;
}

// 文件结束时强制恢复设备
void AudioOutput::resume()
{
    if (m_closing.load() || m_paused.load() || m_deviceResumed.load()) return;
    if (!m_stream) return;

    if (SDL_ResumeAudioStreamDevice(m_stream)) {
        m_deviceResumed.store(true);
    } else {
        logging::ERROR_LOG("AudioOutput::resume", SDL_GetError());
    }
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

// 暂停音频流输出设备
void AudioOutput::pause(bool paused)
{
    if (!m_stream) return;
    m_paused.store(paused);

    if (paused) {
        SDL_PauseAudioStreamDevice(m_stream);
        return;
    }

    if (m_deviceResumed.load()) {
        SDL_ResumeAudioStreamDevice(m_stream);
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

// 关闭音频输出设备
void AudioOutput::close()
{
    m_closing.store(true);

    // 唤醒可能阻塞在 write 的生产者
    if (m_ring) m_ring->abort();

    // 先停止并销毁 SDL 流 确保回调不再运行
    if (m_stream) {
        SDL_PauseAudioStreamDevice(m_stream);
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }

    // 最后释放环形缓冲区
    m_ring.reset();
    m_prebufferBytes = 0;
    m_deviceResumed.store(false);
    m_paused.store(false);
    m_closing.store(false);

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
    if (!m_stream || !m_ring) return;
    if (m_ring->availableRead() < m_prebufferBytes) return;

    if (SDL_ResumeAudioStreamDevice(m_stream)) {
        m_deviceResumed.store(true);
    } else {
        logging::ERROR_LOG("AudioOutput::maybeResume", SDL_GetError());
    }
}

// SDL 音频回调 从环形缓冲区读取数据并推入 SDL 流
void SDLCALL AudioOutput::streamCallback(void *userdata, SDL_AudioStream *stream,
                                         int additionalAmount, int totalAmount)
{
    (void)totalAmount;

    // PCM 数据
    auto *self = static_cast<AudioOutput *>(userdata);
    if (!self || self->m_closing.load() || self->m_paused.load()) return;
    if (additionalAmount <= 0 || !self->m_ring) return;

    uint8_t buffer[kCallbackBufferBytes];
    size_t remaining = static_cast<size_t>(additionalAmount);

    while (remaining > 0) {
        const size_t want = std::min(remaining, sizeof(buffer));
        const size_t n = self->m_ring->read(buffer, want);
        if (n == 0) {
            // 队列空 SDL 本次会用静音补齐
            self->m_underruns.fetch_add(1, std::memory_order_relaxed);
            break;
        }

        // 向音频设备推送PCM数据
        if (!SDL_PutAudioStreamData(stream, buffer, static_cast<int>(n)))
            break;

        remaining -= n;
    }
}

} // namespace avplayer
