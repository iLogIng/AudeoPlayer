#include "media/MediaTimeline.hpp"

namespace avplayer
{

int64_t MediaTimeline::normalize(const AVStream *stream, int64_t timestampUs) const
{
    if (!stream || timestampUs == AV_NOPTS_VALUE)
        return timestampUs;

    int64_t startUs = 0;
    if (stream->start_time != AV_NOPTS_VALUE) {
        startUs = av_rescale_q(
            stream->start_time, stream->time_base, AV_TIME_BASE_Q);
    } else if (m_format && m_format->start_time != AV_NOPTS_VALUE) {
        // 流未提供起始时间时退回到容器起始时间
        startUs = m_format->start_time;
    }

    return timestampUs - startUs;
}

} // namespace avplayer
