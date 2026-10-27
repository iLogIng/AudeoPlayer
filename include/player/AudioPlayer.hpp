#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

#include "core/PacketQueue.hpp"
#include "media/AudioDecoder.hpp"
#include "media/Demuxer.hpp"
#include "media/Resampler.hpp"
#include "output/AudioOutput.hpp"

namespace avplayer
{

enum class PlayerEvent
{
    AudioDecodeEof, // 解码结束 所有 PCM 已送入输出
    Error,   // worker 线程异常
};

// 音频播放器 负责编排解封装、解码、重采样、输出
class AudioPlayer
{
public:
    using PlayerEventCallback = std::function<void(PlayerEvent)>;

    explicit AudioPlayer(const std::filesystem::path& path);
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&)            = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    void start();
    void stop();
    void pause(bool paused);

    // 必须在 start() 之前调用
    void setEventCallback(PlayerEventCallback callback);

    bool finished() const;
    uint32_t drainDurationMs() const;
    const AudioClock& audioClock() const;
    std::exception_ptr error() const;

private:
    void demuxLoop(std::stop_token st);
    void decodeLoop(std::stop_token st);
    void onWorkerExit(std::exception_ptr error);
    void setError(std::exception_ptr error);
    void notifyEvent(PlayerEvent event);

    std::filesystem::path m_path;
    Demuxer m_demuxer;
    AudioDecoder m_decoder;
    Resampler m_resampler;
    AudioOutput m_output;
    PacketQueue m_packetQueue;

    std::jthread m_demuxThread;
    std::jthread m_decodeThread;
    std::atomic<bool> m_decodeDone{false};
    std::atomic<bool> m_started{false};

    mutable std::mutex m_errorMutex;
    std::exception_ptr m_error;

    mutable std::mutex m_callbackMutex;
    PlayerEventCallback m_eventCallback;

}; // class AudioPlayer

} // namespace avplayer
