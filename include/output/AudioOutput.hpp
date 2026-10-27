#pragma once

#include <SDL3/SDL_audio.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>

#include "output/PcmRingBuffer.hpp"
#include "sync/AudioClock.hpp"

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
    bool push(const uint8_t *pcmData, size_t bytes, int64_t ptsUs, std::stop_token st);
    // 文件结束时强制恢复设备 处理短于预缓冲的文件
    void resume();
    // 标记所有 PCM 已写入 避免把尾部的静音补齐统计为欠载
    void markEndOfStream();
    size_t queuedBytes() const;
    void pause(bool paused);
    void setVolume(float volume);
    // 仅唤醒可能阻塞在 ring 写入的生产者 不销毁设备
    void abort();
    void close();

    const SDL_AudioSpec& spec() const { return m_spec; }
    const AudioClock& clock() const { return m_clock; }
    size_t underrunCount() const { return m_underruns.load(); }

    uint32_t bytesPerSecond() const { return static_cast<uint32_t>(m_bytesPerSecond); }
    uint32_t deviceBufferDurationMs() const { return m_deviceBufferDurationMs; }
    // 剩余排队数据 + 设备缓冲的估算时长
    uint32_t drainDurationMs() const;

private:
    static void SDLCALL streamCallback(void *userdata, SDL_AudioStream *stream,
                                       int additionalAmount, int totalAmount);
    void maybeResume();

    SDL_AudioStream *m_stream = nullptr;
    SDL_AudioSpec m_spec{};
    std::unique_ptr<PcmRingBuffer> m_ring;
    size_t m_prebufferBytes = 0;
    size_t m_bytesPerSecond = 0;
    size_t m_frameBytes = 0;
    uint32_t m_deviceBufferDurationMs = 0;
    bool m_basePtsSet = false;

    AudioClock m_clock;

    std::atomic<bool> m_closing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_deviceResumed{false};
    std::atomic<bool> m_endOfStream{false};
    std::atomic<uint64_t> m_underruns{0};

}; // class AudioOutput

} // namespace avplayer
