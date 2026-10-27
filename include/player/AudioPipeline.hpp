#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stop_token>

#include "core/PacketQueue.hpp"
#include "media/AudioDecoder.hpp"
#include "media/Resampler.hpp"
#include "output/AudioOutput.hpp"
#include "sync/AudioClock.hpp"

namespace avplayer
{

// 音频处理流水线
// 由 MediaPlayer 的音频线程调用 run()
class AudioPipeline
{
public:
    AudioPipeline() = default;
    ~AudioPipeline() = default;

    AudioPipeline(const AudioPipeline&)            = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    void open(const AVStream *stream, AVRational timeBase);
    void close();

    // 返回 true 表示正常到达 EOF 并完成 flush
    bool run(PacketQueue& packets, std::stop_token st);

    void abort();
    void pause(bool paused);

    uint32_t drainDurationMs() const;
    const AudioClock& clock() const;
    bool decodeDone() const;
    size_t queuedBytes() const;

private:
    bool drainFrames(std::stop_token st);
    bool flushAll(std::stop_token st);

    AudioDecoder m_decoder;
    Resampler m_resampler;
    AudioOutput m_output;
    ffmpeg::FramePtr m_frame;
    PcmChunk m_chunk;
    std::atomic<bool> m_decodeDone{false};

}; // class AudioPipeline

} // namespace avplayer
