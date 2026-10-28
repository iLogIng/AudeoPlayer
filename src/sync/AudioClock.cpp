#include "sync/AudioClock.hpp"

#include <chrono>

namespace avplayer
{

// 当前所在时间点
int64_t AudioClock::nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 发布
void AudioClock::publish(int64_t positionUs, int64_t timeNs)
{
    const uint32_t seq = m_seq.load(std::memory_order_relaxed);

    m_seq.store(seq + 1, std::memory_order_release); // odd: 写入中
    // 更新锚点位置
    m_anchorPositionUs.store(positionUs, std::memory_order_relaxed);
    // 更新锚点时间
    m_anchorTimeNs.store(timeNs, std::memory_order_relaxed);
    m_seq.store(seq + 2, std::memory_order_release); // even: 写入完成
}

// 重置
void AudioClock::reset(int sampleRate, int64_t basePtsUs)
{
    m_sampleRate.store(sampleRate);
    m_deviceLatencyUs.store(0);

    m_basePtsUs = basePtsUs;
    m_consumedFrames = 0;

    const int64_t now = nowNs();
    m_pauseAccumNs.store(0, std::memory_order_release);
    m_pauseStartNs.store(now, std::memory_order_release);
    m_paused.store(true, std::memory_order_release);

    publish(basePtsUs, now);
}

// 设置基准PTS
void AudioClock::setBasePtsUs(int64_t ptsUs)
{
    m_basePtsUs = ptsUs;
    m_consumedFrames = 0;
    publish(ptsUs, nowNs());
}

void AudioClock::setDeviceLatencyUs(int64_t latencyUs)
{
    m_deviceLatencyUs.store(latencyUs > 0 ? latencyUs : 0, std::memory_order_release);
}

// 推进时间点记录
void AudioClock::advance(int64_t frames, int64_t bufferedUs)
{
    if (frames > 0)
        m_consumedFrames += frames;

    const int64_t sampleRate = m_sampleRate.load(std::memory_order_acquire);
    if (sampleRate <= 0) return;

    // 位置更新
    int64_t position = m_basePtsUs
                     + m_consumedFrames * 1'000'000 / sampleRate
                     - (bufferedUs > 0 ? bufferedUs : 0)
                     - m_deviceLatencyUs.load(std::memory_order_acquire);

    if (position < m_basePtsUs)
        position = m_basePtsUs;

    publish(position, nowNs());
}

void AudioClock::pause()
{
    if (m_paused.load(std::memory_order_acquire)) return;

    m_pauseStartNs.store(nowNs(), std::memory_order_release);
    m_paused.store(true, std::memory_order_release);
}

// 时钟唤醒
void AudioClock::resume()
{
    if (!m_paused.load(std::memory_order_acquire)) return;

    // 当前时间点
    const int64_t now = nowNs();
    // 开始时间点
    const int64_t start = m_pauseStartNs.load(std::memory_order_acquire);
    // 累积时间
    const int64_t accumulated = m_pauseAccumNs.load(std::memory_order_acquire);

    m_pauseAccumNs.store(accumulated + (now - start), std::memory_order_release);
    m_paused.store(false, std::memory_order_release);
}

int64_t AudioClock::positionUs() const
{
    int64_t anchorPositionUs = 0;
    int64_t anchorTimeNs = 0;
    uint32_t seq1 = 0;
    uint32_t seq2 = 0;

    // seqlock 读取 保证锚点字段来自同一个快照
    // 联动publish
    do {
        seq1 = m_seq.load(std::memory_order_acquire);
        if (seq1 & 1u) continue;    // 写入中

        anchorPositionUs = m_anchorPositionUs.load(std::memory_order_relaxed);
        anchorTimeNs = m_anchorTimeNs.load(std::memory_order_relaxed);

        seq2 = m_seq.load(std::memory_order_acquire);
    } while (seq1 != seq2 || (seq2 & 1u));  // 写入中

    const bool paused = m_paused.load(std::memory_order_acquire);
    const int64_t effectiveNow = paused
        ? m_pauseStartNs.load(std::memory_order_acquire)
        : nowNs();
    const int64_t pauseAccumNs = m_pauseAccumNs.load(std::memory_order_acquire);

    int64_t elapsedNs = effectiveNow - anchorTimeNs - pauseAccumNs;
    if (elapsedNs < 0) elapsedNs = 0;

    return anchorPositionUs + elapsedNs / 1000;
}

bool AudioClock::paused() const
{
    return m_paused.load(std::memory_order_acquire);
}

bool AudioClock::valid() const
{
    return m_sampleRate.load(std::memory_order_acquire) > 0;
}

} // namespace avplayer
