#pragma once

#include <memory>

#include "ffmpeg_compat.hpp"

namespace avplayer
{
namespace ffmpeg
{

// FFmpeg 资源统一使用 unique_ptr 管理
struct PacketDeleter
{
    void operator()(AVPacket *pkt) const {
        if (pkt) av_packet_free(&pkt);
    }
};

struct FrameDeleter
{
    void operator()(AVFrame *frame) const {
        if (frame) av_frame_free(&frame);
    }
};

struct FormatContextDeleter
{
    void operator()(AVFormatContext *ctx) const {
        if (ctx) avformat_close_input(&ctx);
    }
};

struct CodecContextDeleter
{
    void operator()(AVCodecContext *ctx) const {
        if (ctx) avcodec_free_context(&ctx);
    }
};

struct SwrContextDeleter
{
    void operator()(SwrContext *ctx) const {
        if (ctx) swr_free(&ctx);
    }
};

using PacketPtr        = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr         = std::unique_ptr<AVFrame, FrameDeleter>;
using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr  = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using SwrContextPtr    = std::unique_ptr<SwrContext, SwrContextDeleter>;

inline PacketPtr makePacket() { return PacketPtr(av_packet_alloc()); }
inline FramePtr  makeFrame() { return FramePtr(av_frame_alloc()); }

} // namespace ffmpeg
} // namespace avplayer
