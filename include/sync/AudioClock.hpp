#pragma once

#include <atomic>
#include <cstdint>

namespace avplayer
{

// 被动的音频播放时钟
// 只负责提供音频当前播放位置，不拥有线程/定时器/事件调度
class AudioClock
{
public:
    AudioClock() = default;
    ~AudioClock() = default;

    AudioClock(const AudioClock&)            = delete;
    AudioClock& operator=(const AudioClock&) = delete;

    // 仅在没有并发音频回调时调用
    void reset(int sampleRate, int64_t basePtsUs = 0);

    void setBasePtsUs(int64_t ptsUs);
    void setDeviceLatencyUs(int64_t latencyUs);

    // 音频回调 更新已消费帧数并发布新的锚点
    void advance(int64_t frames, int64_t bufferedUs);

    void pause();
    void resume();

    int64_t positionUs() const;
    bool paused() const;
    bool valid() const;

private:
    static int64_t nowNs();
    void publish(int64_t positionUs, int64_t timeNs);

    std::atomic<int64_t> m_sampleRate{0};
    std::atomic<int64_t> m_deviceLatencyUs{0};

    // 暂停状态只由控制线程写，音频回调不直接修改
    std::atomic<bool>    m_paused{true};
    std::atomic<int64_t> m_pauseStartNs{0};
    std::atomic<int64_t> m_pauseAccumNs{0};

    // seqlock 快照
    std::atomic<uint32_t> m_seq{0};
    std::atomic<int64_t>  m_anchorPositionUs{0};    // 位置锚点
    std::atomic<int64_t>  m_anchorTimeNs{0};        // 时间锚点

    // 仅由 reset() 或音频回调写
    int64_t m_basePtsUs = 0;
    int64_t m_consumedFrames = 0;

}; // class AudioClock

} // namespace avplayer
