#pragma once

#include "FFmpegRAII.hpp"

namespace avplayer
{

// 将解码帧转换为 YUV420P 供 VideoOutput 使用
class VideoConverter
{
public:
    VideoConverter() = default;
    ~VideoConverter() = default;

    VideoConverter(const VideoConverter&)            = delete;
    VideoConverter& operator=(const VideoConverter&) = delete;

    // 返回 YUV420P 帧
    ffmpeg::FramePtr convert(const AVFrame *src);

private:
    bool ensureContext(const AVFrame *src);

    ffmpeg::SwsContextPtr m_swsctx;
    AVPixelFormat m_srcFormat = AV_PIX_FMT_NONE;
    int m_srcWidth = 0;
    int m_srcHeight = 0;

}; // class VideoConverter

} // namespace avplayer
