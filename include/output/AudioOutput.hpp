#pragma once

#include <SDL3/SDL_audio.h>
#include <cstddef>
#include <cstdint>
#include <stop_token>

namespace avplayer
{

// SDL 音频输出
// 使用 push 模型将 PCM 送入 SDL_AudioStream
class AudioOutput
{
public:
    AudioOutput() = default;
    ~AudioOutput();

    AudioOutput(const AudioOutput&)            = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void open();
    // 有界写入，队列过长时阻塞等待 可被 stop_token 中断
    bool push(const uint8_t *data, size_t bytes, std::stop_token st);
    size_t queuedBytes() const;
    void pause(bool paused);
    void setVolume(float volume);
    void close();

    const SDL_AudioSpec& spec() const { return m_spec; }

private:
    void waitForSpace(std::stop_token st);

    SDL_AudioStream *m_stream = nullptr;
    SDL_AudioSpec m_spec{};
    size_t m_maxQueuedBytes = 0;

}; // class AudioOutput

} // namespace avplayer
