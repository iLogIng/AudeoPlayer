#pragma once

#include <filesystem>

#include "FFmpegRAII.hpp"

namespace avplayer
{

// 音视频解封装器 只负责打开输入、查找流、读取数据包
class Demuxer
{
public:
    Demuxer() = default;
    ~Demuxer() = default;

    Demuxer(const Demuxer&)            = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    void open(const std::filesystem::path& path);
    // 返回 <0 表示结束或错误
    int readPacket(AVPacket *pkt);
    void close();

    int audioStreamIndex() const { return m_audioStreamIdx; }
    const AVStream *audioStream() const;
    AVFormatContext *formatContext() const { return m_fmtctx.get(); }
    AVRational audioTimeBase() const;

private:
    ffmpeg::FormatContextPtr m_fmtctx;
    int m_audioStreamIdx = -1;

}; // class Demuxer

} // namespace avplayer
