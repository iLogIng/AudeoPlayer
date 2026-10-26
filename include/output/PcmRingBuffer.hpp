#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <vector>

namespace avplayer
{

// 生产者: 解码线程，阻塞写入
// 消费者: SDL 音频回调，非阻塞读取
// PCM 环形缓冲区
class PcmRingBuffer
{
public:
    explicit PcmRingBuffer(size_t capacityBytes);
    ~PcmRingBuffer() = default;

    PcmRingBuffer(const PcmRingBuffer&)            = delete;
    PcmRingBuffer& operator=(const PcmRingBuffer&) = delete;

    // 生产者: 阻塞直到整块数据写入完成或 stop
    bool write(const uint8_t *data, size_t bytes, std::stop_token st);
    // 消费者: 非阻塞读取 返回实际读取的字节数
    size_t read(uint8_t *dst, size_t maxBytes);

    size_t availableRead() const;
    size_t availableWrite() const;
    size_t capacity() const;

    void clear();
    // 唤醒所有阻塞的生产者
    void abort();

private:
    size_t availableWriteLocked() const;

    std::vector<uint8_t> m_data;
    size_t m_capacity = 0;
    size_t m_readPos  = 0;
    size_t m_writePos = 0;
    size_t m_size     = 0;
    bool   m_aborted  = false;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_notFull;

}; // class PcmRingBuffer

} // namespace avplayer
