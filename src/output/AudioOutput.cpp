#include "output/AudioOutput.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

#include "Log.hpp"

namespace avplayer
{

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

    // 采用push模式打开音频设备流
    m_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &m_spec, nullptr, nullptr);
    if (!m_stream) {
        logging::FAILED_LOG("AudioOutput::open", SDL_GetError());
        throw std::runtime_error("audio output: can not open audio stream");
    }

    // 最多缓冲约 1 秒的音频数据 至少缓冲1B
    // freq * channels * bytesPerSmaple
    const size_t cachedSec = 1;
    m_maxQueuedBytes = cachedSec
                     * static_cast<size_t>(m_spec.freq)
                     * static_cast<size_t>(m_spec.channels)
                     * bytesPerSample;
    if (m_maxQueuedBytes == 0)
        m_maxQueuedBytes = 1;

    // 恢复音频流设备
    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        logging::FAILED_LOG("AudioOutput::open", SDL_GetError());
        throw std::runtime_error("audio output: can not resume audio stream");
    }
}

// 推送数据
bool AudioOutput::push(const uint8_t *pcmData, size_t bytes, std::stop_token st)
{
    if (!m_stream) return false;
    if (bytes == 0) return true;

    // 等待空间
    waitForSpace(st);
    if (st.stop_requested()) return false;

    // 推送音频流数据
    if (!SDL_PutAudioStreamData(m_stream, pcmData, static_cast<int>(bytes))) {
        logging::ERROR_LOG("AudioOutput::push", SDL_GetError());
        return false;
    }
    return true;
}

// 队列字节长度
size_t AudioOutput::queuedBytes() const
{
    if (!m_stream) return 0;
    const int queued = SDL_GetAudioStreamQueued(m_stream);
    return queued > 0 ? static_cast<size_t>(queued) : 0;
}

// 暂停音频流输出设备
void AudioOutput::pause(bool paused)
{
    if (!m_stream) return;
    if (paused) {
        // 暂停
        SDL_PauseAudioStreamDevice(m_stream);
    } else {
        // 恢复
        SDL_ResumeAudioStreamDevice(m_stream);
    }
}

// 设置音量
void AudioOutput::setVolume(float volume)
{
    if (!m_stream) return;
    // 设置响度
    if (!SDL_SetAudioStreamGain(m_stream, volume)) {
        logging::ERROR_LOG("AudioOutput::setVolume", SDL_GetError());
    }
}

void AudioOutput::close()
{
    if (!m_stream) return;
    SDL_DestroyAudioStream(m_stream);
    m_stream = nullptr;
    m_maxQueuedBytes = 0;
}

// 等待缓冲区空间
void AudioOutput::waitForSpace(std::stop_token st)
{
    while (!st.stop_requested() && queuedBytes() > m_maxQueuedBytes) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace avplayer
