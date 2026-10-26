#pragma once

#include "FFmpegRAII.hpp"

namespace avplayer
{

// 音频解码器 只负责将压缩包解码成 AVFrame
class AudioDecoder
{
public:
    AudioDecoder() = default;
    ~AudioDecoder() = default;

    AudioDecoder(const AudioDecoder&)            = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    void open(const AVStream *stream);
    // pkt 为 nullptr 时进入 flush 模式
    int send(const AVPacket *pkt);
    int receive(AVFrame *frame);
    void flush();
    void close();

    AVCodecContext *context() const { return m_codectx.get(); }

private:
    ffmpeg::CodecContextPtr m_codectx;

}; // class AudioDecoder

} // namespace avplayer
