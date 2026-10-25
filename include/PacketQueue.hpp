#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <libavcodec/packet.h>
#include <mutex>
namespace avplayer
{

// 数据包队列
class PacketQueue
{
public:
    // 拷贝入队
    void push(AVPacket *pkt);
    // 提取数据包 检查是否应该中断
    int pop(AVPacket *out, bool& abort);
    // 唤醒所有阻塞的 pop
    void abort();
    // 刷新
    void flush();
    size_t size();

private:
    std::deque<AVPacket*> m_pktque;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_abort = false;
    size_t m_bytes = 0;
};

} // namespace avplayer

