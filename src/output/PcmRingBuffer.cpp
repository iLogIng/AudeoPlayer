#include "output/PcmRingBuffer.hpp"

#include <algorithm>
#include <cstring>

namespace avplayer
{

PcmRingBuffer::PcmRingBuffer(size_t capacityBytes)
    : m_data(capacityBytes > 0 ? capacityBytes : 1)
    , m_capacity(capacityBytes > 0 ? capacityBytes : 1)
{
}

// 生产写入PCM数据
bool PcmRingBuffer::write(const uint8_t *pcmData, size_t bytes, std::stop_token st)
{
    if (bytes == 0) return true;
    // 单次写入不能超过整个缓冲区
    if (!pcmData || bytes > m_capacity) return false;

    std::unique_lock lock(m_mutex);
    // 等待PCM数据消费
    m_notFull.wait(lock, st, [this, bytes] {
        return m_aborted || availableWriteLocked() >= bytes;
    });

    if (m_aborted || st.stop_requested()) return false;

    // 处理回绕 最多分两段拷贝
    const size_t first = std::min(bytes, m_capacity - m_writePos);
    std::memcpy(m_data.data() + m_writePos, pcmData, first);
    if (bytes > first)
        std::memcpy(m_data.data(), pcmData + first, bytes - first);

    m_writePos = (m_writePos + bytes) % m_capacity;
    m_size += bytes;
    return true;
}

// 消费者读取数据 非阻塞
// 返回真实读取的数据量
size_t PcmRingBuffer::read(uint8_t *dst, size_t maxBytes)
{
    if (!dst || maxBytes == 0) return 0;

    size_t n = 0;
    {
        std::lock_guard lock(m_mutex);
        n = std::min(maxBytes, m_size);
        if (n == 0) return 0;

        // 处理回绕 最多分两段拷贝
        const size_t first = std::min(n, m_capacity - m_readPos);
        std::memcpy(dst, m_data.data() + m_readPos, first);
        if (n > first)
            std::memcpy(dst + first, m_data.data(), n - first);

        m_readPos = (m_readPos + n) % m_capacity;
        m_size -= n;
    }

    // 唤醒PCM数据生产
    m_notFull.notify_one();
    return n;
}

// 可读的数据大小
size_t PcmRingBuffer::availableRead() const
{
    std::lock_guard lock(m_mutex);
    return m_size;
}

// 可写的空间大小
size_t PcmRingBuffer::availableWrite() const
{
    std::lock_guard lock(m_mutex);
    return availableWriteLocked();
}

// 缓冲的总容量
size_t PcmRingBuffer::capacity() const
{
    return m_capacity;
}

// 清空缓冲
void PcmRingBuffer::clear()
{
    std::lock_guard lock(m_mutex);
    m_readPos  = 0;
    m_writePos = 0;
    m_size     = 0;
    m_notFull.notify_all();
}

// 中断
void PcmRingBuffer::abort()
{
    std::lock_guard lock(m_mutex);
    m_aborted = true;
    m_notFull.notify_all();
}

// 写可锁
size_t PcmRingBuffer::availableWriteLocked() const
{
    return m_capacity - m_size;
}

} // namespace avplayer
