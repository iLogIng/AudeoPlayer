#include "media/MediaTimeline.hpp"

namespace avplayer
{

int64_t MediaTimeline::normalize(const AVStream *stream, int64_t timestampUs) const
{
    if (!stream || timestampUs == AV_NOPTS_VALUE)
        return timestampUs;

    if (stream->start_time == AV_NOPTS_VALUE)
        return timestampUs;

    const int64_t startUs = av_rescale_q(
        stream->start_time, stream->time_base, AV_TIME_BASE_Q);

    return timestampUs - startUs;
}

} // namespace avplayer
