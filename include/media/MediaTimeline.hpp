#pragma once

#include "FFmpegRAII.hpp"

namespace avplayer
{

// 媒体时间线
// 负责把各流的 PTS 转换到统一的播放时间线
class MediaTimeline
{
public:
    MediaTimeline() = default;
    explicit MediaTimeline(const AVFormatContext *format)
        : m_format(format) { }

    void configure(const AVFormatContext *format) { m_format = format; }

    // 当前策略
    // 减去所属流的 start_time，将各流对齐到 0
    int64_t normalize(const AVStream *stream, int64_t timestampUs) const;

private:
    const AVFormatContext *m_format = nullptr;

}; // class MediaTimeline

} // namespace avplayer
