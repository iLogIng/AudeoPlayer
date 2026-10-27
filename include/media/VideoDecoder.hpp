#pragma once

#include "FFmpegRAII.hpp"

namespace avplayer
{

// 视频解码器 只负责把压缩包解码成 AVFrame
class VideoDecoder
{
public:
    VideoDecoder() = default;
    ~VideoDecoder() = default;

    VideoDecoder(const VideoDecoder&)            = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    void open(const AVStream *stream);
    // pkt 为 nullptr 时进入 flush 模式
    int send(const AVPacket *pkt);
    int receive(AVFrame *frame);
    void flush();
    void close();

    AVCodecContext *context() const { return m_codectx.get(); }

private:
    ffmpeg::CodecContextPtr m_codectx;

}; // class VideoDecoder

} // namespace avplayer
