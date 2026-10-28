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

class MediaTimeline;

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
              const MediaTimeline *timeline,
              SDL_Renderer *renderer,
              FrameReadyCallback onFrameReady);
    void close();

    // 丢弃 seek 目标之前的视频帧 只能在 open() 之后调用
    void setSeekTargetUs(int64_t targetUs);

    // 返回 true 表示正常到达 EOF 并收完所有帧
    bool run(PacketQueue& packets, std::stop_token st);

    void abort();

    bool hasFrame() const;
    bool nextPtsUs(int64_t& ptsUs) const;
    bool presentNextFrame(); // 主线程
    bool dropNextFrame();    // 主线程 丢弃过期帧
    int64_t frameDurationUs() const;
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
    const AVStream *m_stream = nullptr;
    const MediaTimeline *m_timeline = nullptr;

    AVRational m_timeBase{0, 1};
    int64_t m_lastPtsUs = 0;
    int64_t m_frameDurationUs = 33333;
    int64_t m_seekTargetUs = -1;
    bool m_hasPts = false;

    std::atomic<bool> m_decodeDone{false};

}; // class VideoPipeline

} // namespace avplayer
