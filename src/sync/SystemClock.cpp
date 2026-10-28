#include "sync/SystemClock.hpp"

#include <chrono>

namespace avplayer
{

int64_t SystemClock::nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SystemClock::start(int64_t basePtsUs)
{
    const int64_t now = nowNs();

    m_basePtsUs.store(basePtsUs);
    m_anchorTimeNs.store(now);
    m_pauseStartNs.store(now);
    m_pauseAccumNs.store(0);
    m_paused.store(false);
    m_valid.store(true);
}

void SystemClock::pause()
{
    if (m_paused.load(std::memory_order_acquire)) return;

    m_pauseStartNs.store(nowNs(), std::memory_order_release);
    m_paused.store(true, std::memory_order_release);
}

void SystemClock::resume()
{
    if (!m_paused.load(std::memory_order_acquire)) return;

    const int64_t now = nowNs();
    const int64_t start = m_pauseStartNs.load(std::memory_order_acquire);
    const int64_t accumulated = m_pauseAccumNs.load(std::memory_order_acquire);

    m_pauseAccumNs.store(accumulated + (now - start), std::memory_order_release);
    m_paused.store(false, std::memory_order_release);
}

int64_t SystemClock::positionUs() const
{
    const int64_t basePtsUs = m_basePtsUs.load();
    if (!m_valid.load(std::memory_order_acquire))
        return basePtsUs;

    const int64_t effectiveNow = m_paused.load(std::memory_order_acquire)
        ? m_pauseStartNs.load(std::memory_order_acquire)
        : nowNs();
    const int64_t anchorTimeNs = m_anchorTimeNs.load(std::memory_order_acquire);
    const int64_t pauseAccumNs = m_pauseAccumNs.load(std::memory_order_acquire);

    int64_t elapsedNs = effectiveNow - anchorTimeNs - pauseAccumNs;
    if (elapsedNs < 0) elapsedNs = 0;

    return basePtsUs + elapsedNs / 1000;
}

bool SystemClock::paused() const
{
    return m_paused.load(std::memory_order_acquire);
}

bool SystemClock::valid() const
{
    return m_valid.load(std::memory_order_acquire);
}

} // namespace avplayer
