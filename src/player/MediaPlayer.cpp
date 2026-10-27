#include "player/MediaPlayer.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

#include "Log.hpp"

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

void MediaPlayer::start()
{
    if (m_started.exchange(true)) return;

    try {
        m_demuxer.open(m_path);

        const AVStream *audioStream = m_demuxer.audioStream();
        if (!audioStream) {
            throw std::runtime_error("media player: audio stream is required");
        }

        m_audioEof.store(false);
        m_audio.open(audioStream, m_demuxer.audioTimeBase());
    } catch (...) {
        m_started.store(false);
        throw;
    }

    m_demuxThread = std::jthread([this](std::stop_token st) {
        try {
            demuxLoop(st);
        } catch (...) {
            onWorkerExit(std::current_exception());
        }
    });

    m_audioThread = std::jthread([this](std::stop_token st) {
        try {
            audioLoop(st);
        } catch (...) {
            onWorkerExit(std::current_exception());
        }
    });
}

void MediaPlayer::stop()
{
    m_demuxThread.request_stop();
    m_audioThread.request_stop();
    m_audioPackets.abort();
    m_audio.abort();

    if (m_audioThread.joinable()) m_audioThread.join();
    if (m_demuxThread.joinable()) m_demuxThread.join();

    m_audio.close();
    m_demuxer.close();
    m_started.store(false);
}

void MediaPlayer::pause(bool paused)
{
    m_audio.pause(paused);
}

void MediaPlayer::setEventCallback(EventCallback callback)
{
    std::lock_guard lock(m_callbackMutex);
    m_eventCallback = std::move(callback);
}

bool MediaPlayer::hasVideo() const
{
    return m_demuxer.videoStreamIndex() >= 0;
}

bool MediaPlayer::finished() const
{
    return m_audioEof.load() && m_audio.queuedBytes() == 0;
}

uint32_t MediaPlayer::drainDurationMs() const
{
    return m_audio.drainDurationMs();
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
            return;
        }
        if (ret < 0) {
            logging::FAILED_LOG("MediaPlayer::demuxLoop", "can not read packet");
            throw std::runtime_error("media player: can not read packet");
        }

        if (packet->stream_index == m_demuxer.audioStreamIndex()) {
            if (m_audioPackets.push(std::move(packet), st) != QueueStatus::Ok)
                return;

            packet = ffmpeg::makePacket();
        } else {
            // Phase 0: 视频包暂时丢弃
            av_packet_unref(packet.get());
        }
    }
}

void MediaPlayer::audioLoop(std::stop_token st)
{
    try {
        if (m_audio.run(m_audioPackets, st)) {
            m_audioEof.store(true);
            notifyEvent(PlayerEvent::AudioDecodeEof);
        }
    } catch (...) {
        onWorkerExit(std::current_exception());
    }
}

void MediaPlayer::onWorkerExit(std::exception_ptr error)
{
    if (error) {
        setError(error);
        notifyEvent(PlayerEvent::Error);
    }

    m_audioEof.store(true);
    m_audioPackets.abort();
    m_audio.abort();

    // 仅请求停止 不能在 worker 内 join 自己
    m_demuxThread.request_stop();
    m_audioThread.request_stop();
}

void MediaPlayer::setError(std::exception_ptr error)
{
    std::lock_guard lock(m_errorMutex);
    if (!m_error) m_error = error;
}

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
