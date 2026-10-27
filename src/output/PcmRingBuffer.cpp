#include "output/PcmRingBuffer.hpp"

#include <algorithm>
#include <cstring>

namespace avplayer
{

PcmRingBuffer::PcmRingBuffer(size_t capacityBytes)
    : m_capacity(capacityBytes > 0 ? capacityBytes : 1)
    , m_data(m_capacity)
{
}

// 生产者写入数据 队列满时阻塞
bool PcmRingBuffer::write(const uint8_t *pcmData, size_t bytes, std::stop_token st)
{
    if (bytes == 0) return true;
    if (!pcmData || bytes > m_capacity) return false;

    while (true) {
        // 先取通知序号 再检查中止和空间 避免丢失唤醒
        const size_t seq = m_spaceSeq.load(std::memory_order_acquire);
        if (m_aborted.load(std::memory_order_acquire) || st.stop_requested())
            return false;

        const size_t readPos  = m_readPos.load(std::memory_order_acquire);
        const size_t writePos = m_writePos.load(std::memory_order_relaxed);
        const size_t used     = writePos - readPos;

        if (m_capacity - used >= bytes) {
            // 处理回绕 最多分两段拷贝
            const size_t index = writePos % m_capacity;
            const size_t first = std::min(bytes, m_capacity - index);
            std::memcpy(m_data.data() + index, pcmData, first);
            if (bytes > first)
                std::memcpy(m_data.data(), pcmData + first, bytes - first);

            m_writePos.store(writePos + bytes, std::memory_order_release);
            return true;
        }

        // 等待消费者释放空间 stop 由 abort() 唤醒
        m_spaceSeq.wait(seq, std::memory_order_acquire);
    }
}

// 消费者读取数据 非阻塞
size_t PcmRingBuffer::read(uint8_t *dst, size_t maxBytes)
{
    if (!dst || maxBytes == 0) return 0;

    const size_t writePos = m_writePos.load(std::memory_order_acquire);
    const size_t readPos  = m_readPos.load(std::memory_order_relaxed);
    const size_t n        = std::min(maxBytes, writePos - readPos);
    if (n == 0) return 0;

    // 处理回绕 最多分两段拷贝
    const size_t index = readPos % m_capacity;
    const size_t first = std::min(n, m_capacity - index);
    std::memcpy(dst, m_data.data() + index, first);
    if (n > first)
        std::memcpy(dst + first, m_data.data(), n - first);

    m_readPos.store(readPos + n, std::memory_order_release);
    m_spaceSeq.fetch_add(1, std::memory_order_release);
    // 生产者可生产
    m_spaceSeq.notify_one();
    return n;
}

// 剩余可读数据量
size_t PcmRingBuffer::availableRead() const
{
    // 先读 readPos 再读 writePos 避免第三方线程查询时出现无符号下溢
    const size_t readPos  = m_readPos.load(std::memory_order_acquire);
    const size_t writePos = m_writePos.load(std::memory_order_acquire);
    return writePos - readPos;
}

// 可写入数据量
size_t PcmRingBuffer::availableWrite() const
{
    const size_t available = availableRead();
    return available < m_capacity ? m_capacity - available : 0;
}

size_t PcmRingBuffer::capacity() const
{
    return m_capacity;
}

void PcmRingBuffer::clear()
{
    m_readPos.store(0, std::memory_order_release);
    m_writePos.store(0, std::memory_order_release);
    m_spaceSeq.fetch_add(1, std::memory_order_release);
    m_spaceSeq.notify_all();
}

void PcmRingBuffer::abort()
{
    m_aborted.store(true, std::memory_order_release);
    m_spaceSeq.fetch_add(1, std::memory_order_release);
    m_spaceSeq.notify_all();
}

} // namespace avplayer
