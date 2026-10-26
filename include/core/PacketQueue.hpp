#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stop_token>

#include "FFmpegRAII.hpp"

namespace avplayer
{

enum class QueueStatus
{
    Ok,       // 成功
    Eof,      // 队列结束
    Stopped,  // 被中断
}; // enum class QueueStatus

// 线程安全的有界数据包队列
class PacketQueue
{
public:
    explicit PacketQueue(size_t maxPackets = 256, size_t maxBytes = 16 * 1024 * 1024);

    // 入队，队列满时阻塞
    QueueStatus push(ffmpeg::PacketPtr pkt, std::stop_token st);
    // 出队，队列空时阻塞
    QueueStatus pop(ffmpeg::PacketPtr& out, std::stop_token st);

    // 标记结束，等待中的 pop 会取完剩余数据后返回 Eof
    void pushEof();
    // 清空队列
    void flush();
    // 终止并唤醒所有阻塞的线程
    void abort();

    size_t packetCount() const;
    size_t byteCount() const;

private:
    std::deque<ffmpeg::PacketPtr> m_pktque;
    size_t m_bytes = 0;                         // 队列中字节数
    size_t m_maxPackets = 256;                  // 最大包数量
    size_t m_maxBytes = 16 * 1024 * 1024;       // 最大字节数
    bool m_aborted = false;
    bool m_eof = false;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_notEmpty;
    std::condition_variable_any m_notFull;

}; // class PacketQueue

} // namespace avplayer
