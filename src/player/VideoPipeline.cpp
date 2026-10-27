#include "player/VideoPipeline.hpp"

#include <new>
#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

void VideoPipeline::open(const AVStream *stream,
                         AVRational timeBase,
                         SDL_Renderer *renderer,
                         FrameReadyCallback onFrameReady)
{
    if (!stream || !renderer) {
        throw std::invalid_argument("video pipeline: invalid stream or renderer");
    }

    m_timeBase = (timeBase.num > 0 && timeBase.den > 0)
        ? timeBase
        : AVRational{1, AV_TIME_BASE};

    m_decoder.open(stream);
    m_output = std::make_unique<VideoOutput>(renderer);
    m_onFrameReady = std::move(onFrameReady);

    m_frame = ffmpeg::makeFrame();
    m_frames.reset();

    m_lastPtsUs = 0;
    m_hasPts = false;
    m_frameDurationUs = 33333;

    AVRational rate = stream->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        rate = stream->r_frame_rate;
    if (rate.num > 0 && rate.den > 0) {
        const int64_t duration = av_rescale_q(
            1, av_inv_q(rate), AV_TIME_BASE_Q);
        if (duration > 0)
            m_frameDurationUs = duration;
    }

    m_decodeDone.store(false);
}

void VideoPipeline::close()
{
    m_onFrameReady = nullptr;
    m_output.reset();
    m_frames.flush();
    m_frame.reset();
    m_decoder.close();
    m_decodeDone.store(false);
}

bool VideoPipeline::run(PacketQueue& packets, std::stop_token st)
{
    if (!m_frame)
        m_frame = ffmpeg::makeFrame();

    m_decodeDone.store(false);

    ffmpeg::PacketPtr packet;
    while (!st.stop_requested()) {
        const QueueStatus status = packets.pop(packet, st);

        if (status == QueueStatus::Stopped)
            return false;

        if (status == QueueStatus::Eof) {
            if (!flushAll(st))
                return false;
            m_decodeDone.store(true);
            return true;
        }

        int ret = m_decoder.send(packet.get());
        int retry = 0;
        while (ret == AVERROR(EAGAIN) && retry++ < 4) {
            if (!drainFrames(st))
                return false;
            ret = m_decoder.send(packet.get());
        }

        if (ret == AVERROR(EAGAIN)) {
            logging::FAILED_LOG("VideoPipeline::run", "decoder is stuck");
            throw std::runtime_error("video pipeline: decoder is stuck");
        }
        if (ret < 0 && ret != AVERROR_EOF) {
            logging::FAILED_LOG("VideoPipeline::run", "can not send packet");
            throw std::runtime_error("video pipeline: can not send packet");
        }

        if (!drainFrames(st))
            return false;
    }

    return false;
}

bool VideoPipeline::drainFrames(std::stop_token st)
{
    while (!st.stop_requested()) {
        const int ret = m_decoder.receive(m_frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        if (ret < 0) {
            logging::FAILED_LOG("VideoPipeline::drainFrames", "can not receive frame");
            throw std::runtime_error("video pipeline: can not receive frame");
        }

        const int64_t timestamp = m_frame->best_effort_timestamp != AV_NOPTS_VALUE
            ? m_frame->best_effort_timestamp
            : m_frame->pts;

        int64_t ptsUs = 0;
        if (timestamp != AV_NOPTS_VALUE) {
            ptsUs = av_rescale_q(timestamp, m_timeBase, AV_TIME_BASE_Q);
        } else if (m_hasPts) {
            ptsUs = m_lastPtsUs + m_frameDurationUs;
        }

        m_lastPtsUs = ptsUs;
        m_hasPts = true;

        ffmpeg::FramePtr converted = m_converter.convert(m_frame.get());
        av_frame_unref(m_frame.get());

        if (!converted) {
            logging::FAILED_LOG("VideoPipeline::drainFrames", "can not convert frame");
            throw std::runtime_error("video pipeline: can not convert frame");
        }

        const size_t bytes = static_cast<size_t>(converted->width)
                           * static_cast<size_t>(converted->height)
                           * 3 / 2;

        VideoFrame frame;
        frame.frame = std::move(converted);
        frame.ptsUs = ptsUs;
        frame.bytes = bytes;

        if (!m_frames.push(std::move(frame), st))
            return false;

        if (m_onFrameReady)
            m_onFrameReady();
    }

    return false;
}

bool VideoPipeline::flushAll(std::stop_token st)
{
    const int ret = m_decoder.send(nullptr);
    if (ret < 0 && ret != AVERROR_EOF) {
        logging::FAILED_LOG("VideoPipeline::flushAll", "can not flush decoder");
        throw std::runtime_error("video pipeline: can not flush decoder");
    }

    return drainFrames(st);
}

void VideoPipeline::abort()
{
    m_frames.abort();
}

bool VideoPipeline::hasFrame() const
{
    return !m_frames.empty();
}

bool VideoPipeline::nextPtsUs(int64_t& ptsUs) const
{
    return m_frames.tryPeekPtsUs(ptsUs);
}

bool VideoPipeline::presentNextFrame()
{
    VideoFrame frame;
    if (!m_frames.tryPop(frame))
        return false;
    if (!m_output)
        return false;

    return m_output->present(frame.frame.get());
}

bool VideoPipeline::decodeDone() const
{
    return m_decodeDone.load();
}

} // namespace avplayer
