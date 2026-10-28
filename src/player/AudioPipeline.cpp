#include "player/AudioPipeline.hpp"

#include <stdexcept>
#include <utility>

#include "Log.hpp"
#include "media/MediaTimeline.hpp"

namespace avplayer
{

void AudioPipeline::open(const AVStream *stream, AVRational timeBase,
                         const MediaTimeline *timeline)
{
    m_stream = stream;
    m_timeline = timeline;

    m_decoder.open(stream);
    m_output.open();
    m_resampler.configure(m_decoder.context(), m_output.spec(), timeBase);
    m_frame = ffmpeg::makeFrame();
    m_seekTargetUs = -1;
    m_decodeDone.store(false);
}

void AudioPipeline::close()
{
    m_output.close();
    m_decoder.close();
    m_frame.reset();
    m_stream = nullptr;
    m_timeline = nullptr;
    m_seekTargetUs = -1;
    m_decodeDone.store(false);
}

void AudioPipeline::setSeekTargetUs(int64_t targetUs)
{
    m_seekTargetUs = targetUs;
}

void AudioPipeline::setStartedCallback(StartedCallback callback)
{
    m_output.setStartedCallback(std::move(callback));
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
            if (!pushChunk(st))
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
        if (!pushChunk(st))
            return false;
    }

    return true;
}

bool AudioPipeline::pushChunk(std::stop_token st)
{
    int64_t ptsUs = m_chunk.ptsUs;
    if (m_timeline && m_stream)
        ptsUs = m_timeline->normalize(m_stream, ptsUs);

    // seek 后 demuxer 通常停在目标之前的关键帧 丢弃目标之前的音频
    if (m_seekTargetUs >= 0 && m_chunk.sampleFrames > 0) {
        const int rate = m_resampler.outputSampleRate();
        if (rate > 0) {
            const int64_t durationUs =
                static_cast<int64_t>(m_chunk.sampleFrames) * 1'000'000 / rate;

            if (ptsUs + durationUs <= m_seekTargetUs)
                return true;

            if (ptsUs < m_seekTargetUs) {
                const int channels = m_resampler.outputChannels();
                const int bytesPerSample =
                    av_get_bytes_per_sample(m_resampler.outputFormat());
                const int64_t frameBytes =
                    static_cast<int64_t>(bytesPerSample) * channels;
                // 跳过帧
                const int64_t skipFrames =
                    (m_seekTargetUs - ptsUs) * rate / 1'000'000;

                if (skipFrames >= m_chunk.sampleFrames)
                    return true;

                // 跳过字节
                const size_t skipBytes =
                    static_cast<size_t>(skipFrames * frameBytes);
                m_chunk.data.erase(
                    m_chunk.data.begin(),
                    m_chunk.data.begin() + static_cast<std::ptrdiff_t>(skipBytes));
                // 更新采样帧
                m_chunk.sampleFrames -= static_cast<int>(skipFrames);
                ptsUs = m_seekTargetUs;
            }

            m_seekTargetUs = -1;
        }
    }

    return m_output.push(m_chunk.data.data(), m_chunk.data.size(), ptsUs, st);
}

void AudioPipeline::abort()
{
    m_output.abort();
}

void AudioPipeline::pause(bool paused)
{
    m_output.pause(paused);
}

void AudioPipeline::setVolume(float volume)
{
    m_output.setVolume(volume);
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
