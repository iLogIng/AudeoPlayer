#include "player/AudioPipeline.hpp"

#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

void AudioPipeline::open(const AVStream *stream, AVRational timeBase)
{
    m_decoder.open(stream);
    m_output.open();
    m_resampler.configure(m_decoder.context(), m_output.spec(), timeBase);
    m_frame = ffmpeg::makeFrame();
    m_decodeDone.store(false);
}

void AudioPipeline::close()
{
    m_output.close();
    m_decoder.close();
    m_frame.reset();
    m_decodeDone.store(false);
}

// 处理并推送音频流
bool AudioPipeline::run(PacketQueue& packets, std::stop_token st)
{
    if (!m_frame)
        m_frame = ffmpeg::makeFrame();

    m_decodeDone.store(false);

    while (!st.stop_requested()) {
        ffmpeg::PacketPtr packet;
        const QueueStatus status = packets.pop(packet, st);

        if (status == QueueStatus::Stopped)
            return false;

        if (status == QueueStatus::Eof) {
            if (!flushAll(st))
                return false;

            m_output.markEndOfStream();
            m_output.resume();
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
            logging::FAILED_LOG("AudioPipeline::run", "decoder is stuck");
            throw std::runtime_error("audio pipeline: decoder is stuck");
        }
        if (ret < 0 && ret != AVERROR_EOF) {
            logging::FAILED_LOG("AudioPipeline::run", "can not send packet");
            throw std::runtime_error("audio pipeline: can not send packet");
        }

        if (!drainFrames(st))
            return false;
    }

    return false;
}

bool AudioPipeline::drainFrames(std::stop_token st)
{
    while (!st.stop_requested()) {
        const int ret = m_decoder.receive(m_frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        if (ret < 0) {
            logging::FAILED_LOG("AudioPipeline::drainFrames", "can not receive frame");
            throw std::runtime_error("audio pipeline: can not receive frame");
        }

        const bool converted = m_resampler.convert(m_frame.get(), m_chunk);
        av_frame_unref(m_frame.get());
        if (converted && !m_chunk.data.empty()) {
            if (!m_output.push(m_chunk.data.data(), m_chunk.data.size(), m_chunk.ptsUs, st))
                return false;
        }
    }

    return false;
}

bool AudioPipeline::flushAll(std::stop_token st)
{
    const int ret = m_decoder.send(nullptr);
    if (ret < 0 && ret != AVERROR_EOF) {
        logging::FAILED_LOG("AudioPipeline::flushAll", "can not flush decoder");
        throw std::runtime_error("audio pipeline: can not flush decoder");
    }

    if (!drainFrames(st))
        return false;

    while (m_resampler.flush(m_chunk)) {
        if (!m_output.push(m_chunk.data.data(), m_chunk.data.size(), m_chunk.ptsUs, st))
            return false;
    }

    return true;
}

void AudioPipeline::abort()
{
    m_output.abort();
}

void AudioPipeline::pause(bool paused)
{
    m_output.pause(paused);
}

uint32_t AudioPipeline::drainDurationMs() const
{
    return m_output.drainDurationMs();
}

const AudioClock& AudioPipeline::clock() const
{
    return m_output.clock();
}

bool AudioPipeline::decodeDone() const
{
    return m_decodeDone.load();
}

size_t AudioPipeline::queuedBytes() const
{
    return m_output.queuedBytes();
}

} // namespace avplayer
