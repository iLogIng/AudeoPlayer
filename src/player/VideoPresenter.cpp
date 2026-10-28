#include "player/VideoPresenter.hpp"

#include <algorithm>

#include "Log.hpp"
#include "core/AppEvent.hpp"
#include "player/MediaPlayer.hpp"

namespace avplayer
{

namespace
{

// 同步阈值上下界 后续可在此调整
constexpr int64_t kMinSyncThresholdUs = 40'000; // 40ms
constexpr int64_t kMaxSyncThresholdUs = 100'000; // 100ms
constexpr int kMaxFramesPerCycle = 8;

Uint64 SDLCALL onFrameTimer(void*, SDL_TimerID, Uint64)
{
    // 队列满时 1ms 后重试
    return AppEvent::push(AppEventCode::FrameDue) ? 0 : 1'000'000;
}

} // namespace

VideoPresenter::VideoPresenter(MediaPlayer& player)
    : m_player(player)
{
}

VideoPresenter::~VideoPresenter()
{
    cancel();
}

void VideoPresenter::onFrameReady()
{
    if (!m_player.masterClock().valid() || m_player.masterClock().paused())
        return;

    presentDueFrames();
    scheduleNext();
}

void VideoPresenter::onFrameDue()
{
    m_timer = 0;

    if (!m_player.masterClock().valid() || m_player.masterClock().paused())
        return;

    presentDueFrames();
    scheduleNext();
}

void VideoPresenter::onClockStarted()
{
    if (!m_player.masterClock().valid() || m_player.masterClock().paused())
        return;

    presentDueFrames();
    scheduleNext();
}

void VideoPresenter::cancel()
{
    if (m_timer) {
        SDL_RemoveTimer(m_timer);
        m_timer = 0;
    }
}

void VideoPresenter::presentDueFrames()
{
    const int64_t audioPosUs = m_player.masterClock().positionUs();
    const int64_t syncThresholdUs = std::clamp(
        m_player.videoFrameDurationUs(),
        kMinSyncThresholdUs,
        kMaxSyncThresholdUs);

    int processed = 0;
    while (processed < kMaxFramesPerCycle) {
        int64_t nextPtsUs = 0;
        if (!m_player.nextVideoPtsUs(nextPtsUs))
            break;

        // 计算视频与音频位置偏移
        const int64_t diffUs = nextPtsUs - audioPosUs;

        // 视频落后过多 丢弃该帧
        if (diffUs < -syncThresholdUs) {
            m_player.dropNextVideoFrame();
            ++processed;
            continue;
        }

        // 视频超前 等待下一次 FrameDue
        if (diffUs > syncThresholdUs)
            break;

        if (!m_player.presentNextVideoFrame())
            break;

        ++processed;
    }
}

void VideoPresenter::scheduleNext()
{
    if (m_timer) return;
    if (!m_player.masterClock().valid() || m_player.masterClock().paused())
        return;

    int64_t nextPtsUs = 0;
    if (!m_player.nextVideoPtsUs(nextPtsUs))
        return;

    const int64_t audioPosUs = m_player.masterClock().positionUs();
    const int64_t syncThresholdUs = std::clamp(
        m_player.videoFrameDurationUs(),
        kMinSyncThresholdUs,
        kMaxSyncThresholdUs);

    const int64_t delayUs = std::max<int64_t>(
        nextPtsUs - audioPosUs - syncThresholdUs, 0);
    const Uint64 intervalNs = std::max<Uint64>(
        static_cast<Uint64>(delayUs) * 1000, 1);

    m_timer = SDL_AddTimerNS(intervalNs, onFrameTimer, nullptr);
    if (!m_timer) {
        logging::ERROR_LOG("VideoPresenter::scheduleNext", SDL_GetError());
    }
}

} // namespace avplayer
