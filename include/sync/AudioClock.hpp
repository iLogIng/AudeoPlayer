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

    void reset(int sampleRate);

    void setBasePtsUs(int64_t ptsUs);
    // 音频回调已送入 SDL 的帧数
    void onFramesConsumed(int64_t frames);
    // SDL 内部尚未播放的数据时长
    void setBufferedUs(int64_t bufferedUs);
    // 设备硬件缓冲延迟
    void setDeviceLatencyUs(int64_t latencyUs);

    void pause();
    void resume();

    int64_t positionUs() const;
    bool paused() const;
    bool valid() const;

private:
    std::atomic<int64_t> m_sampleRate{0};       // 采样率
    std::atomic<int64_t> m_basePtsUs{0};        // 基准播放时间戳
    std::atomic<int64_t> m_consumedFrames{0};   // 已消费帧数
    std::atomic<int64_t> m_bufferedUs{0};       // 缓冲的时长
    std::atomic<int64_t> m_deviceLatencyUs{0};  // 设备缓冲延迟
    std::atomic<bool>    m_paused{false};       // 播放停止

}; // class AudioClock

} // namespace avplayer
