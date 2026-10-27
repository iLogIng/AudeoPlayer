#include "media/VideoDecoder.hpp"

#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

void VideoDecoder::open(const AVStream *stream)
{
    if (!stream) {
        logging::FAILED_LOG("VideoDecoder::open", "stream is null");
        throw std::invalid_argument("video decoder: stream is null");
    }

    m_codectx.reset();

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        logging::FAILED_LOG("VideoDecoder::open", "can not find decoder");
        throw std::runtime_error("video decoder: can not find decoder");
    }

    AVCodecContext *codectx = avcodec_alloc_context3(codec);
    if (!codectx) {
        logging::FAILED_LOG("VideoDecoder::open", "can not alloc codec context");
        throw std::runtime_error("video decoder: can not alloc codec context");
    }
    m_codectx.reset(codectx);

    if (avcodec_parameters_to_context(m_codectx.get(), stream->codecpar) < 0) {
        logging::FAILED_LOG("VideoDecoder::open", "can not copy codec parameters");
        throw std::runtime_error("video decoder: can not copy codec parameters");
    }

    m_codectx->pkt_timebase = stream->time_base;
    m_codectx->thread_count = 0;

    if (avcodec_open2(m_codectx.get(), codec, nullptr) < 0) {
        logging::FAILED_LOG("VideoDecoder::open", "can not open codec");
        throw std::runtime_error("video decoder: can not open codec");
    }
}

int VideoDecoder::send(const AVPacket *pkt)
{
    if (!m_codectx) return AVERROR_EOF;
    return avcodec_send_packet(m_codectx.get(), pkt);
}

int VideoDecoder::receive(AVFrame *frame)
{
    if (!m_codectx) return AVERROR_EOF;
    return avcodec_receive_frame(m_codectx.get(), frame);
}

void VideoDecoder::flush()
{
    if (m_codectx) avcodec_flush_buffers(m_codectx.get());
}

void VideoDecoder::close()
{
    m_codectx.reset();
}

} // namespace avplayer
