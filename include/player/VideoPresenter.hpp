#pragma once

#include <SDL3/SDL_timer.h>
#include <cstdint>

namespace avplayer
{

class MediaPlayer;

// 视频展示节奏控制器
// 以主时钟为基准安排视频帧的等待/展示/丢帧
class VideoPresenter
{
public:
    explicit VideoPresenter(MediaPlayer& player);
    ~VideoPresenter();

    VideoPresenter(const VideoPresenter&)            = delete;
    VideoPresenter& operator=(const VideoPresenter&) = delete;

    void onFrameReady();
    void onFrameDue();
    void onClockStarted();
    void cancel();

private:
    void presentDueFrames();
    void scheduleNext();

    MediaPlayer& m_player;
    SDL_TimerID m_timer = 0;

}; // class VideoPresenter

} // namespace avplayer
