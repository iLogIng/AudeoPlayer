#pragma once

#include <atomic>
#include <cstdint>

#include "sync/MasterClock.hpp"

namespace avplayer
{

// 系统单调时钟
// 用于 video-only 或没有音频主时钟的场景
class SystemClock : public MasterClock
{
public:
    SystemClock() = default;
    ~SystemClock() = default;

    SystemClock(const SystemClock&)            = delete;
    SystemClock& operator=(const SystemClock&) = delete;

    void start(int64_t basePtsUs = 0);
    void pause();
    void resume();

    int64_t positionUs() const override;
    bool paused() const override;
    bool valid() const override;

private:
    static int64_t nowNs();

    std::atomic<int64_t> m_basePtsUs{0};
    std::atomic<int64_t> m_anchorTimeNs{0};
    std::atomic<int64_t> m_pauseStartNs{0};
    std::atomic<int64_t> m_pauseAccumNs{0};
    std::atomic<bool> m_paused{true};
    std::atomic<bool> m_valid{false};

}; // class SystemClock

} // namespace avplayer
