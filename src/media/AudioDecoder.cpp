#include "media/AudioDecoder.hpp"

#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

void AudioDecoder::open(const AVStream *stream)
{
    if (!stream) {
        logging::FAILED_LOG("AudioDecoder::open", "stream is null");
        throw std::invalid_argument("audio decoder: stream is null");
    }

    // 编解码器 选择解码器
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        logging::FAILED_LOG("AudioDecoder::open", "can not find decoder");
        throw std::runtime_error("audio decoder: can not find decoder");
    }

    // 编解码器上下文
    AVCodecContext *codectx = avcodec_alloc_context3(codec);
    if (!codectx) {
        logging::FAILED_LOG("AudioDecoder::open", "can not alloc codec context");
        throw std::runtime_error("audio decoder: can not alloc codec context");
    }
    m_codectx.reset(codectx);

    // 获取编解码器参数
    if (avcodec_parameters_to_context(m_codectx.get(), stream->codecpar) < 0) {
        logging::FAILED_LOG("AudioDecoder::open", "can not copy codec parameters");
        throw std::runtime_error("audio decoder: can not copy codec parameters");
    }

    // 打开编解码器上下文
    if (avcodec_open2(m_codectx.get(), codec, nullptr) < 0) {
        logging::FAILED_LOG("AudioDecoder::open", "can not open codec");
        throw std::runtime_error("audio decoder: can not open codec");
    }
}

// 发送待解码数据包
int AudioDecoder::send(const AVPacket *pkt)
{
    if (!m_codectx) return AVERROR_EOF;
    return avcodec_send_packet(m_codectx.get(), pkt);
}

// 接收已解码数据
int AudioDecoder::receive(AVFrame *frame)
{
    if (!m_codectx) return AVERROR_EOF;
    return avcodec_receive_frame(m_codectx.get(), frame);
}

// 重置解码状态
void AudioDecoder::flush()
{
    if (m_codectx) avcodec_flush_buffers(m_codectx.get());
}

// 关闭解码器
void AudioDecoder::close()
{
    m_codectx.reset();
}

} // namespace avplayer
