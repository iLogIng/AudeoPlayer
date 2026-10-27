#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <vector>

namespace avplayer
{

// 单生产者/单消费者的无锁 PCM 环形缓冲区
// 生产者 解码线程，可阻塞
// 消费者 SDL 音频回调线程 线程安全
class PcmRingBuffer
{
public:
    explicit PcmRingBuffer(size_t capacityBytes);
    ~PcmRingBuffer() = default;

    PcmRingBuffer(const PcmRingBuffer&)            = delete;
    PcmRingBuffer& operator=(const PcmRingBuffer&) = delete;

    // 生产者 阻塞直到整块数据写入完成或 stop
    bool write(const uint8_t *data, size_t bytes, std::stop_token st);
    // 消费者 非阻塞读取 返回实际读取的字节数
    size_t read(uint8_t *dst, size_t maxBytes);

    size_t availableRead() const;
    size_t availableWrite() const;
    size_t capacity() const;

    // 仅在生产者/消费者均已暂停或停止时调用
    void clear();
    // 唤醒所有阻塞的生产者
    void abort();

private:
    size_t m_capacity = 0;
    size_t m_mask     = 0;
    std::vector<uint8_t> m_data;

    // 单调递增的位置 写入时用低位掩码定位
    std::atomic<size_t> m_readPos{0};
    std::atomic<size_t> m_writePos{0};
    // 空间变化/中止通知序号 用于唤醒生产者
    std::atomic<size_t> m_spaceSeq{0};
    std::atomic<bool>   m_aborted{false};

}; // class PcmRingBuffer

} // namespace avplayer
