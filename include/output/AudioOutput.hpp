#pragma once

#include <SDL3/SDL_audio.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>

#include "output/PcmRingBuffer.hpp"

namespace avplayer
{

// SDL 音频输出
// 解码线程写入 PcmRingBuffer，SDL 音频回调从 ring 读取并推入 SDL_AudioStream
class AudioOutput
{
public:
    AudioOutput() = default;
    ~AudioOutput();

    AudioOutput(const AudioOutput&)            = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void open();
    // 写入环形缓冲区 队列满时阻塞 可被 stop_token 中断
    bool push(const uint8_t *pcmData, size_t bytes, std::stop_token st);
    // 文件结束时强制恢复设备 处理短于预缓冲的文件
    void resume();
    size_t queuedBytes() const;
    void pause(bool paused);
    void setVolume(float volume);
    void close();

    const SDL_AudioSpec& spec() const { return m_spec; }
    size_t underrunCount() const { return m_underruns.load(); }

private:
    static void SDLCALL streamCallback(void *userdata, SDL_AudioStream *stream,
                                       int additionalAmount, int totalAmount);
    void maybeResume();

    SDL_AudioStream *m_stream = nullptr;
    SDL_AudioSpec m_spec{};
    std::unique_ptr<PcmRingBuffer> m_ring;
    size_t m_prebufferBytes = 0;

    std::atomic<bool> m_closing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_deviceResumed{false};
    std::atomic<uint64_t> m_underruns{0};

}; // class AudioOutput

} // namespace avplayer
