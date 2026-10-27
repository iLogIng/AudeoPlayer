#include "media/VideoConverter.hpp"

#include <new>
#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

// 确认上下文
bool VideoConverter::ensureContext(const AVFrame *src)
{
    // 像素格式
    const auto format = static_cast<AVPixelFormat>(src->format);
    if (m_swsctx && format == m_srcFormat
        && src->width == m_srcWidth && src->height == m_srcHeight)
        return true;

    SwsContext *ctx = sws_getContext(
        src->width, src->height, format,
        src->width, src->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!ctx) {
        logging::FAILED_LOG("VideoConverter::ensureContext", "can not create sws context");
        return false;
    }

    m_swsctx.reset(ctx);
    m_srcFormat = format;
    m_srcWidth = src->width;
    m_srcHeight = src->height;
    return true;
}

ffmpeg::FramePtr VideoConverter::convert(const AVFrame *src)
{
    if (!src) return {};

    // 已经是 YUV420P 直接引用原帧 避免拷贝
    if (src->format == AV_PIX_FMT_YUV420P) {
        AVFrame *clone = av_frame_clone(src);
        if (!clone) {
            logging::FAILED_LOG("VideoConverter::convert", "can not clone frame");
            throw std::bad_alloc();
        }
        return ffmpeg::FramePtr(clone);
    }

    if (!ensureContext(src)) {
        throw std::runtime_error("video converter: can not create sws context");
    }

    // 转换目的
    ffmpeg::FramePtr frame = ffmpeg::makeFrame();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width  = src->width;
    frame->height = src->height;

    if (av_frame_get_buffer(frame.get(), 32) < 0) {
        logging::FAILED_LOG("VideoConverter::convert", "can not alloc output frame");
        throw std::runtime_error("video converter: can not alloc output frame");
    }

    // 缩放
    const int ret = sws_scale(
        m_swsctx.get(),
        src->data, src->linesize, 0, src->height,
        frame->data, frame->linesize);
    if (ret < 0) {
        logging::FAILED_LOG("VideoConverter::convert", "sws_scale failed");
        throw std::runtime_error("video converter: sws_scale failed");
    }

    return frame;
}

} // namespace avplayer
