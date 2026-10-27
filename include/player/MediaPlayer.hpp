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
#include "player/AudioPipeline.hpp"
#include "player/PlayerEvent.hpp"
#include "sync/AudioClock.hpp"

namespace avplayer
{

// 媒体播放器 统一编排解封装与音频/视频流水线
class MediaPlayer
{
public:
    using EventCallback = std::function<void(PlayerEvent)>;

    explicit MediaPlayer(const std::filesystem::path& path);
    ~MediaPlayer();

    MediaPlayer(const MediaPlayer&)            = delete;
    MediaPlayer& operator=(const MediaPlayer&) = delete;

    void start();
    void stop();
    void pause(bool paused);

    // 必须在 start() 之前调用
    void setEventCallback(EventCallback callback);

    bool hasVideo() const;
    bool finished() const;
    uint32_t drainDurationMs() const;
    const AudioClock& audioClock() const;
    std::exception_ptr error() const;

private:
    void demuxLoop(std::stop_token st);
    void audioLoop(std::stop_token st);
    void onWorkerExit(std::exception_ptr error);
    void setError(std::exception_ptr error);
    void notifyEvent(PlayerEvent event);

    std::filesystem::path m_path;
    Demuxer m_demuxer;
    PacketQueue m_audioPackets;
    AudioPipeline m_audio;

    std::jthread m_demuxThread;
    std::jthread m_audioThread;

    std::atomic<bool> m_started{false};
    std::atomic<bool> m_audioEof{false};

    mutable std::mutex m_errorMutex;
    std::exception_ptr m_error;

    mutable std::mutex m_callbackMutex;
    EventCallback m_eventCallback;

}; // class MediaPlayer

} // namespace avplayer
