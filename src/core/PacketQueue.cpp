#include "core/PacketQueue.hpp"

#include <utility>

namespace avplayer
{

PacketQueue::PacketQueue(size_t maxPackets, size_t maxBytes)
    : m_maxPackets(maxPackets)
    , m_maxBytes(maxBytes)
{
}

QueueStatus PacketQueue::push(ffmpeg::PacketPtr pkt, std::stop_token st)
{
    if (!pkt) return QueueStatus::Stopped;

    std::unique_lock lock(m_mutex);
    // 等待队列可生产
    m_notFull.wait(lock, st, [this] {
        return m_aborted
            || m_eof
            || (m_pktque.size() < m_maxPackets && m_bytes < m_maxBytes);
    });

    if (m_aborted || m_eof || st.stop_requested())
        return QueueStatus::Stopped;

    m_bytes += static_cast<size_t>(pkt->size);
    m_pktque.push_back(std::move(pkt));
    // 提醒队列可消费
    m_notEmpty.notify_one();
    return QueueStatus::Ok;
}

QueueStatus PacketQueue::pop(ffmpeg::PacketPtr& out, std::stop_token st)
{
    std::unique_lock lock(m_mutex);
    // 等待队列可消费
    m_notEmpty.wait(lock, st, [this] {
        return m_aborted || m_eof || !m_pktque.empty();
    });

    if (m_aborted || st.stop_requested())
        return QueueStatus::Stopped;

    if (m_pktque.empty())
        return QueueStatus::Eof;

    out = std::move(m_pktque.front());
    m_pktque.pop_front();
    if (out)
        m_bytes -= static_cast<size_t>(out->size);

    // 提醒队列可生产
    m_notFull.notify_one();
    return QueueStatus::Ok;
}

void PacketQueue::pushEof()
{
    std::lock_guard lock(m_mutex);
    m_eof = true;
    m_notEmpty.notify_all();
}

void PacketQueue::flush()
{
    std::lock_guard lock(m_mutex);
    m_pktque.clear();
    m_bytes = 0;
    m_eof = false;
    m_notFull.notify_all();
}

void PacketQueue::reset()
{
    std::lock_guard lock(m_mutex);
    m_pktque.clear();
    m_bytes = 0;
    m_aborted = false;
    m_eof = false;
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

void PacketQueue::abort()
{
    std::lock_guard lock(m_mutex);
    m_aborted = true;
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

size_t PacketQueue::packetCount() const
{
    std::lock_guard lock(m_mutex);
    return m_pktque.size();
}

size_t PacketQueue::byteCount() const
{
    std::lock_guard lock(m_mutex);
    return m_bytes;
}

} // namespace avplayer
