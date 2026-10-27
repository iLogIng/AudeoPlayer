#pragma once

#include <SDL3/SDL_timer.h>
#include <cstdint>

namespace avplayer
{

class MediaPlayer;

// 使用系统时钟锚点 按视频 PTS 安排 FrameDue
class VideoPresenter
{
public:
    explicit VideoPresenter(MediaPlayer& player);
    ~VideoPresenter();

    VideoPresenter(const VideoPresenter&)            = delete;
    VideoPresenter& operator=(const VideoPresenter&) = delete;

    void onFrameReady();
    void onFrameDue();
    void cancel();

private:
    int64_t videoNowPtsUs() const;
    void presentDueFrames();
    void scheduleNext();

    MediaPlayer& m_player;
    SDL_TimerID m_timer = 0;
    bool m_anchored = false;
    int64_t m_anchorPtsUs = 0;
    int64_t m_anchorTimeNs = 0;

}; // class VideoPresenter

} // namespace avplayer
