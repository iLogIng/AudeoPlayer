#include "player/VideoPresenter.hpp"

#include <algorithm>
#include <chrono>

#include "Log.hpp"
#include "core/AppEvent.hpp"
#include "player/MediaPlayer.hpp"

namespace avplayer
{

namespace
{

// 当前时间点
int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 帧计时器
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

// 帧就绪
void VideoPresenter::onFrameReady()
{
    presentDueFrames();
    scheduleNext();
}

// 帧到期
void VideoPresenter::onFrameDue()
{
    m_timer = 0;
    presentDueFrames();
    scheduleNext();
}

// 取消播放
void VideoPresenter::cancel()
{
    if (m_timer) {
        SDL_RemoveTimer(m_timer);
        m_timer = 0;
    }
}

// 当前PTS时间戳
int64_t VideoPresenter::videoNowPtsUs() const
{
    if (!m_anchored) return 0;
    return m_anchorPtsUs + (nowNs() - m_anchorTimeNs) / 1000;
}

// 发送到期帧
void VideoPresenter::presentDueFrames()
{
    int64_t firstPtsUs = 0;
    if (!m_anchored) {
        if (!m_player.nextVideoPtsUs(firstPtsUs))
            return;

        m_anchorPtsUs = firstPtsUs;
        m_anchorTimeNs = nowNs();
        m_anchored = true;
    }

    int presented = 0;
    while (presented < 8) {
        int64_t nextPtsUs = 0;
        if (!m_player.nextVideoPtsUs(nextPtsUs))
            break;
        if (nextPtsUs > videoNowPtsUs())
            break;

        if (!m_player.presentNextVideoFrame())
            break;

        ++presented;
    }
}

// 管理下一帧
void VideoPresenter::scheduleNext()
{
    if (m_timer) return;

    // 获取下一帧时间戳
    int64_t nextPtsUs = 0;
    if (!m_player.nextVideoPtsUs(nextPtsUs))
        return;

    const int64_t delayUs = std::max<int64_t>(
        nextPtsUs - videoNowPtsUs(), 0);
    const Uint64 intervalNs = std::max<Uint64>(
        static_cast<Uint64>(delayUs) * 1000, 1);

    // SDL计时器
    m_timer = SDL_AddTimerNS(intervalNs, onFrameTimer, nullptr);
    if (!m_timer) {
        logging::ERROR_LOG("VideoPresenter::scheduleNext", SDL_GetError());
    }
}

} // namespace avplayer
