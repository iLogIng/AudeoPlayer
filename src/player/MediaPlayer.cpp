#include "player/MediaPlayer.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <stdexcept>
#include <utility>

#include "Log.hpp"
#include "core/AppEvent.hpp"
#include "output/Window.hpp"

namespace avplayer
{

MediaPlayer::MediaPlayer(const std::filesystem::path& path)
    : m_path(path)
{
}

MediaPlayer::~MediaPlayer()
{
    stop();
}

void MediaPlayer::open()
{
    if (m_opened) return;
    if (m_started.load())
        throw std::logic_error("media player: can not open after start");

    m_demuxer.open(m_path);
    m_timeline.configure(m_demuxer.formatContext());

    if (!m_demuxer.audioStream() && !m_demuxer.videoStream()) {
        throw std::runtime_error("media player: no audio or video stream");
    }

    m_opened = true;
}

void MediaPlayer::start(Window *window)
{
    if (!m_opened)
        throw std::logic_error("media player: open() must be called first");

    if (m_started.exchange(true)) return;

    try {
        m_window = window;

        const bool hasAudioStream = hasAudio();
        const bool hasVideoStream = hasVideo();

        if (hasVideoStream && window && !window->renderer()) {
            throw std::runtime_error("media player: video window has no renderer");
        }

        m_audioEnabled = hasAudioStream;
        m_videoEnabled = hasVideoStream && window != nullptr;

        if (!m_audioEnabled && !m_videoEnabled) {
            throw std::runtime_error("media player: no playable stream");
        }

        m_audioEof.store(false);
        m_audioDrained.store(!m_audioEnabled);
        m_videoEof.store(false);
        m_videoDrained.store(!m_videoEnabled);
        m_finished.store(false);

        if (m_audioEnabled) {
            m_audio.open(m_demuxer.audioStream(), m_demuxer.audioTimeBase(), &m_timeline);
            if (m_pendingSeekUs >= 0)
                m_audio.setSeekTargetUs(m_pendingSeekUs);
            m_audio.setVolume(m_volume);
            m_audio.setStartedCallback([this] {
                notifyEvent(PlayerEvent::ClockStarted);
            });
        }

        if (m_videoEnabled) {
            m_video.open(
                m_demuxer.videoStream(),
                m_demuxer.videoTimeBase(),
                &m_timeline,
                window->renderer(),
                [this] {
                    AppEvent::push(AppEventCode::VideoFrameReady, m_sessionId.load());
                });
            if (m_pendingSeekUs >= 0)
                m_video.setSeekTargetUs(m_pendingSeekUs);
        } else if (hasVideoStream) {
            logging::INFO_LOG("MediaPlayer::start", "video disabled: no window");
        }

        if (!m_audioEnabled) {
            m_systemClock.start(m_pendingSeekUs > 0 ? m_pendingSeekUs : 0);
        }
    } catch (...) {
        m_started.store(false);
        throw;
    }

    m_audioPackets.reset();
    m_videoPackets.reset();

    m_demuxThread = std::jthread([this](std::stop_token st) {
        try {
            demuxLoop(st);
        } catch (...) {
            onWorkerExit(std::current_exception());
        }
    });

    if (m_audioEnabled) {
        m_audioThread = std::jthread([this](std::stop_token st) {
            try {
                audioLoop(st);
            } catch (...) {
                onWorkerExit(std::current_exception());
            }
        });
    }

    if (m_videoEnabled) {
        m_videoThread = std::jthread([this](std::stop_token st) {
            try {
                videoLoop(st);
            } catch (...) {
                onWorkerExit(std::current_exception());
            }
        });
    }

    if (!m_audioEnabled && m_videoEnabled) {
        notifyEvent(PlayerEvent::ClockStarted);
    }

    // seek 后保持原来的暂停状态
    if (m_paused)
        pause(true);

    m_pendingSeekUs = -1;
}

void MediaPlayer::stop()
{
    m_demuxThread.request_stop();
    m_audioThread.request_stop();
    m_videoThread.request_stop();

    m_audioPackets.abort();
    m_videoPackets.abort();
    m_audio.abort();
    m_video.abort();

    if (m_videoThread.joinable()) m_videoThread.join();
    if (m_audioThread.joinable()) m_audioThread.join();
    if (m_demuxThread.joinable()) m_demuxThread.join();

    m_video.close();
    m_audio.close();
    m_demuxer.close();

    m_started.store(false);
    m_opened = false;
}

void MediaPlayer::pause(bool paused)
{
    m_paused = paused;

    if (m_audioEnabled) {
        m_audio.pause(paused);
        return;
    }

    if (m_videoEnabled) {
        if (paused) {
            m_systemClock.pause();
        } else {
            m_systemClock.resume();
            notifyEvent(PlayerEvent::ClockStarted);
        }
    }
}

void MediaPlayer::setVolume(float volume)
{
    m_volume = std::clamp(volume, 0.0f, 1.0f);

    if (m_audioEnabled)
        m_audio.setVolume(m_volume);
}

int64_t MediaPlayer::durationUs() const
{
    return m_demuxer.durationUs();
}

bool MediaPlayer::seekTo(int64_t targetUs)
{
    if (!m_opened || !m_started)
        return false;

    const int64_t duration = durationUs();
    if (duration > 0)
        targetUs = std::clamp<int64_t>(targetUs, 0, duration);
    else if (targetUs < 0)
        targetUs = 0;

    Window *window = m_window;

    stop();
    open();

    if (m_demuxer.seekTo(targetUs) < 0)
        throw std::runtime_error("media player: seek failed");

    // start() 会丢弃目标之前的音视频帧 并保持暂停状态与音量
    m_pendingSeekUs = targetUs;
    start(window);

    return true;
}

void MediaPlayer::setEventCallback(EventCallback callback)
{
    std::lock_guard lock(m_callbackMutex);
    m_eventCallback = std::move(callback);
}

bool MediaPlayer::hasAudio() const
{
    return m_demuxer.audioStreamIndex() >= 0;
}

bool MediaPlayer::hasVideo() const
{
    return m_demuxer.videoStreamIndex() >= 0;
}

bool MediaPlayer::videoEnabled() const
{
    return m_videoEnabled;
}

bool MediaPlayer::hasPendingVideoFrame() const
{
    return m_videoEnabled && m_video.hasFrame();
}

bool MediaPlayer::nextVideoPtsUs(int64_t& ptsUs) const
{
    return m_videoEnabled && m_video.nextPtsUs(ptsUs);
}

bool MediaPlayer::presentNextVideoFrame()
{
    if (!m_videoEnabled)
        return false;

    const bool presented = m_video.presentNextFrame();
    updateVideoDrained();
    return presented;
}

bool MediaPlayer::dropNextVideoFrame()
{
    if (!m_videoEnabled)
        return false;

    const bool dropped = m_video.dropNextFrame();
    updateVideoDrained();
    return dropped;
}

int64_t MediaPlayer::videoFrameDurationUs() const
{
    return m_video.frameDurationUs();
}

// 更新视频帧耗尽
void MediaPlayer::updateVideoDrained()
{
    if (!m_videoEnabled) {
        m_videoDrained.store(true);
        return;
    }

    if (m_videoEof.load() && !m_video.hasFrame()) {
        m_videoDrained.store(true);
        checkFinished();
    }
}

bool MediaPlayer::finished() const
{
    return m_finished.load();
}

const MasterClock& MediaPlayer::masterClock() const
{
    if (m_audioEnabled)
        return m_audio.clock();
    return m_systemClock;
}

const AudioClock& MediaPlayer::audioClock() const
{
    return m_audio.clock();
}

std::exception_ptr MediaPlayer::error() const
{
    std::lock_guard lock(m_errorMutex);
    return m_error;
}

// 解封装循环
void MediaPlayer::demuxLoop(std::stop_token st)
{
    ffmpeg::PacketPtr packet = ffmpeg::makePacket();

    while (!st.stop_requested()) {
        const int ret = m_demuxer.readPacket(packet.get());

        if (ret == AVERROR(EAGAIN)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (ret == AVERROR_EOF) {
            m_audioPackets.pushEof();
            m_videoPackets.pushEof();
            return;
        }
        if (ret < 0) {
            logging::FAILED_LOG("MediaPlayer::demuxLoop", "can not read packet");
            throw std::runtime_error("media player: can not read packet");
        }

        // 获得音视频流索引
        if (packet->stream_index == m_demuxer.audioStreamIndex()) {
            if (m_audioEnabled) {
                if (m_audioPackets.push(std::move(packet), st) != QueueStatus::Ok)
                    return;
                packet = ffmpeg::makePacket();
            } else {
                av_packet_unref(packet.get());
            }
        } else if (packet->stream_index == m_demuxer.videoStreamIndex()) {
            if (m_videoEnabled) {
                if (m_videoPackets.push(std::move(packet), st) != QueueStatus::Ok)
                    return;
                packet = ffmpeg::makePacket();
            } else {
                av_packet_unref(packet.get());
            }
        } else {
            av_packet_unref(packet.get());
        }
    }
}

// 音频循环
void MediaPlayer::audioLoop(std::stop_token st)
{
    try {
        if (!m_audio.run(m_audioPackets, st))
            return;

        m_audioEof.store(true);

        const uint32_t drainMs = m_audio.drainDurationMs();
        if (drainMs > 0) {
            std::mutex mutex;
            std::condition_variable_any cv;
            std::unique_lock lock(mutex);
            cv.wait_for(lock, st, std::chrono::milliseconds(drainMs),
                        [] { return false; });
            if (st.stop_requested())
                return;
        }

        m_audioDrained.store(true);
        checkFinished();
    } catch (...) {
        onWorkerExit(std::current_exception());
    }
}

// 视频循环
void MediaPlayer::videoLoop(std::stop_token st)
{
    try {
        if (!m_video.run(m_videoPackets, st))
            return;

        m_videoEof.store(true);
        updateVideoDrained();
    } catch (...) {
        onWorkerExit(std::current_exception());
    }
}

void MediaPlayer::checkFinished()
{
    if (!m_audioDrained.load() || !m_videoDrained.load())
        return;

    bool expected = false;
    if (m_finished.compare_exchange_strong(expected, true))
        notifyEvent(PlayerEvent::PlaybackFinished);
}

void MediaPlayer::onWorkerExit(std::exception_ptr error)
{
    if (error) {
        setError(error);
        notifyEvent(PlayerEvent::Error);
    }

    m_finished.store(true);
    m_audioPackets.abort();
    m_videoPackets.abort();
    m_audio.abort();
    m_video.abort();

    // 仅请求停止 不能在 worker 内 join 自己
    m_demuxThread.request_stop();
    m_audioThread.request_stop();
    m_videoThread.request_stop();
}

void MediaPlayer::setError(std::exception_ptr error)
{
    std::lock_guard lock(m_errorMutex);
    if (!m_error) m_error = error;
}

// 唤醒事件
void MediaPlayer::notifyEvent(PlayerEvent event)
{
    EventCallback callback;
    {
        std::lock_guard lock(m_callbackMutex);
        callback = m_eventCallback;
    }

    if (!callback) return;

    try {
        callback(event);
    } catch (...) {
        // 回调异常不能影响 worker 线程
    }
}

} // namespace avplayer
