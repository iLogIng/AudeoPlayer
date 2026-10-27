#include "media/VideoFrameQueue.hpp"

#include <algorithm>
#include <utility>

namespace avplayer
{

VideoFrameQueue::VideoFrameQueue(size_t maxFrames, size_t maxBytes)
    : m_maxFrames(std::max<size_t>(maxFrames, 1))
    , m_maxBytes(std::max<size_t>(maxBytes, 1))
{
}

bool VideoFrameQueue::push(VideoFrame frame, std::stop_token st)
{
    if (!frame.frame) return false;

    std::unique_lock lock(m_mutex);
    m_notFull.wait(lock, st, [this, &frame] {
        return m_aborted
            || m_frames.empty()
            || (m_frames.size() < m_maxFrames
                && m_bytes + frame.bytes <= m_maxBytes);
    });

    if (m_aborted || st.stop_requested())
        return false;

    m_bytes += frame.bytes;
    m_frames.push_back(std::move(frame));
    return true;
}

bool VideoFrameQueue::tryPop(VideoFrame& out)
{
    std::lock_guard lock(m_mutex);
    if (m_frames.empty()) return false;

    out = std::move(m_frames.front());
    m_frames.pop_front();
    m_bytes -= out.bytes;

    m_notFull.notify_one();
    return true;
}

bool VideoFrameQueue::tryPeekPtsUs(int64_t& ptsUs) const
{
    std::lock_guard lock(m_mutex);
    if (m_frames.empty()) return false;
    ptsUs = m_frames.front().ptsUs;
    return true;
}

size_t VideoFrameQueue::size() const
{
    std::lock_guard lock(m_mutex);
    return m_frames.size();
}

bool VideoFrameQueue::empty() const
{
    std::lock_guard lock(m_mutex);
    return m_frames.empty();
}

void VideoFrameQueue::flush()
{
    std::lock_guard lock(m_mutex);
    m_frames.clear();
    m_bytes = 0;
    m_notFull.notify_all();
}

void VideoFrameQueue::reset()
{
    std::lock_guard lock(m_mutex);
    m_frames.clear();
    m_bytes = 0;
    m_aborted = false;
    m_notFull.notify_all();
}

void VideoFrameQueue::abort()
{
    std::lock_guard lock(m_mutex);
    m_aborted = true;
    m_notFull.notify_all();
}

} // namespace avplayer
