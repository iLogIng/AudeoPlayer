#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

#include "core/PacketQueue.hpp"
#include "media/Demuxer.hpp"
#include "media/MediaTimeline.hpp"
#include "player/AudioPipeline.hpp"
#include "player/PlayerEvent.hpp"
#include "player/VideoPipeline.hpp"
#include "sync/AudioClock.hpp"
#include "sync/MasterClock.hpp"

namespace avplayer
{

class Window;

// 媒体播放器 统一编排解封装与音频/视频流水线
class MediaPlayer
{
public:
    using EventCallback = std::function<void(PlayerEvent)>;

    explicit MediaPlayer(const std::filesystem::path& path);
    ~MediaPlayer();

    MediaPlayer(const MediaPlayer&)            = delete;
    MediaPlayer& operator=(const MediaPlayer&) = delete;

    // 打开 Demuxer 并检查流 不启动线程
    void open();
    // 启动流水线与线程 需要视频时必须提供窗口
    void start(Window *window);
    void stop();
    void pause(bool paused);

    // 必须在 start() 之前调用
    void setEventCallback(EventCallback callback);

    bool hasVideo() const;
    bool hasPendingVideoFrame() const;
    bool nextVideoPtsUs(int64_t& ptsUs) const;
    bool presentNextVideoFrame();   // 主线程
    bool dropNextVideoFrame();      // 主线程
    int64_t videoFrameDurationUs() const;

    bool finished() const;
    const MasterClock& masterClock() const;
    const AudioClock& audioClock() const;
    std::exception_ptr error() const;

private:
    void demuxLoop(std::stop_token st);
    void audioLoop(std::stop_token st);
    void videoLoop(std::stop_token st);
    void onWorkerExit(std::exception_ptr error);
    void setError(std::exception_ptr error);
    void notifyEvent(PlayerEvent event);
    void checkFinished();
    void updateVideoDrained();

    std::filesystem::path m_path;
    Demuxer m_demuxer;
    MediaTimeline m_timeline;
    PacketQueue m_audioPackets;
    PacketQueue m_videoPackets;
    AudioPipeline m_audio;
    VideoPipeline m_video;

    std::jthread m_demuxThread;
    std::jthread m_audioThread;
    std::jthread m_videoThread;

    bool m_opened = false;
    std::atomic<bool> m_started{false};

    std::atomic<bool> m_audioEof{false};
    std::atomic<bool> m_audioDrained{false};
    std::atomic<bool> m_videoEof{false};
    std::atomic<bool> m_videoDrained{false};
    std::atomic<bool> m_finished{false};

    mutable std::mutex m_errorMutex;
    std::exception_ptr m_error;

    mutable std::mutex m_callbackMutex;
    EventCallback m_eventCallback;

}; // class MediaPlayer

} // namespace avplayer
