#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>

#include <SDL3/SDL_render.h>

#include "core/PacketQueue.hpp"
#include "media/VideoConverter.hpp"
#include "media/VideoDecoder.hpp"
#include "media/VideoFrameQueue.hpp"
#include "output/VideoOutput.hpp"

namespace avplayer
{

// 视频处理流水线
// 解码线程负责解码与 YUV420P 转换 主线程负责纹理上传与渲染
class VideoPipeline
{
public:
    using FrameReadyCallback = std::function<void()>;

    VideoPipeline() = default;
    ~VideoPipeline() = default;

    VideoPipeline(const VideoPipeline&)            = delete;
    VideoPipeline& operator=(const VideoPipeline&) = delete;

    void open(const AVStream *stream,
              AVRational timeBase,
              SDL_Renderer *renderer,
              FrameReadyCallback onFrameReady);
    void close();

    // 返回 true 表示正常到达 EOF 并收完所有帧
    bool run(PacketQueue& packets, std::stop_token st);

    void abort();

    bool hasFrame() const;
    bool nextPtsUs(int64_t& ptsUs) const;
    bool presentNextFrame(); // 主线程
    bool decodeDone() const;

private:
    bool drainFrames(std::stop_token st);
    bool flushAll(std::stop_token st);

    VideoDecoder m_decoder;
    VideoConverter m_converter;
    VideoFrameQueue m_frames;
    std::unique_ptr<VideoOutput> m_output;
    FrameReadyCallback m_onFrameReady;
    ffmpeg::FramePtr m_frame;

    AVRational m_timeBase{0, 1};
    int64_t m_lastPtsUs = 0;
    int64_t m_frameDurationUs = 33333;
    bool m_hasPts = false;

    std::atomic<bool> m_decodeDone{false};

}; // class VideoPipeline

} // namespace avplayer
