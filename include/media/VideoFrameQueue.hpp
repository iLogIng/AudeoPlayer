#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stop_token>

#include "FFmpegRAII.hpp"

namespace avplayer
{

// 已转换好的 YUV420P 视频帧
struct VideoFrame
{
    ffmpeg::FramePtr frame;
    int64_t ptsUs = 0;
    size_t bytes = 0;
};

// 有界视频帧队列 生产者阻塞 消费者非阻塞
class VideoFrameQueue
{
public:
    explicit VideoFrameQueue(size_t maxFrames = 12,
                             size_t maxBytes = 128 * 1024 * 1024);

    VideoFrameQueue(const VideoFrameQueue&)            = delete;
    VideoFrameQueue& operator=(const VideoFrameQueue&) = delete;

    bool push(VideoFrame frame, std::stop_token st);
    bool tryPop(VideoFrame& out);
    bool tryPeekPtsUs(int64_t& ptsUs) const;

    size_t size() const;
    bool empty() const;

    void flush();
    // 停止后重新使用前调用
    void reset();
    void abort();

private:
    std::deque<VideoFrame> m_frames;
    size_t m_bytes = 0;
    size_t m_maxFrames = 12;
    size_t m_maxBytes = 128 * 1024 * 1024;
    bool m_aborted = false;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_notFull;

}; // class VideoFrameQueue

} // namespace avplayer
