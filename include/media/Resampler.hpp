#pragma once

#include <SDL3/SDL_audio.h>
#include <cstdint>
#include <vector>

#include "FFmpegRAII.hpp"

namespace avplayer
{

// 重采样后的一段 PCM 数据块
struct PcmChunk
{
    std::vector<uint8_t> data;
    int64_t ptsUs = 0; // 播放帧时间戳（微秒）
}; // struct PcmChunk

// 音频重采样器 将解码帧转换成输出设备需要的格式
class Resampler
{
public:
    Resampler() = default;
    ~Resampler() = default;

    Resampler(const Resampler&)            = delete;
    Resampler& operator=(const Resampler&) = delete;

    void configure(const AVCodecContext *decoder, const SDL_AudioSpec& outputSpec,
                   AVRational timeBase);
    bool convert(const AVFrame *frame, PcmChunk& out);
    // 排空 swr 内部残留的样本
    bool flush(PcmChunk& out);
    void reset();

    AVSampleFormat outputFormat() const { return m_outFormat; }
    int outputSampleRate() const { return m_outSampleRate; }
    int outputChannels() const { return m_outChannels; }

private:
    static AVSampleFormat toAVSampleFormat(SDL_AudioFormat format);

    ffmpeg::SwrContextPtr m_swrctx;
    AVSampleFormat m_outFormat    = AV_SAMPLE_FMT_NONE;
    int m_outSampleRate           = 0;
    int m_outChannels             = 0;
    int m_inSampleRate            = 0;
    AVRational m_timeBase         = {1, AV_TIME_BASE};

}; // class Resampler

} // namespace avplayer
