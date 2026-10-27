#include "sync/AudioClock.hpp"

namespace avplayer
{

// 重置
void AudioClock::reset(int sampleRate)
{
    m_sampleRate.store(sampleRate);
    m_basePtsUs.store(0);
    m_consumedFrames.store(0);
    m_bufferedUs.store(0);
    m_deviceLatencyUs.store(0);
    m_paused.store(false);
}

void AudioClock::setBasePtsUs(int64_t ptsUs)
{
    m_basePtsUs.store(ptsUs);
}

void AudioClock::onFramesConsumed(int64_t frames)
{
    m_consumedFrames.fetch_add(frames);
}

void AudioClock::setBufferedUs(int64_t bufferedUs)
{
    m_bufferedUs.store(bufferedUs > 0 ? bufferedUs : 0);
}

void AudioClock::setDeviceLatencyUs(int64_t latencyUs)
{
    m_deviceLatencyUs.store(latencyUs > 0 ? latencyUs : 0);
}

void AudioClock::pause()
{
    m_paused.store(true);
}

void AudioClock::resume()
{
    m_paused.store(false);
}

// 播放位置
int64_t AudioClock::positionUs() const
{
    const int64_t sampleRate = m_sampleRate.load();
    const int64_t basePtsUs  = m_basePtsUs.load();
    if (sampleRate <= 0) return basePtsUs;

    // 已播放的时间
    const int64_t playedUs = m_consumedFrames.load() * 1'000'000 / sampleRate;
    // 目前的播放位置
    const int64_t position = basePtsUs + playedUs
                           - m_bufferedUs.load()        // 内存缓冲
                           - m_deviceLatencyUs.load();  // 设备缓冲

    // 刚开始播放时设备延迟可能让结果小于基准时间
    return position > basePtsUs ? position : basePtsUs;
}

bool AudioClock::paused() const
{
    return m_paused.load();
}

bool AudioClock::valid() const
{
    return m_sampleRate.load() > 0;
}

} // namespace avplayer
