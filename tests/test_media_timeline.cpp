#include <cstdint>
#include <cstdio>

#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>

#include "media/MediaTimeline.hpp"

namespace
{

int g_failures = 0;

void checkEqual(int64_t actual, int64_t expected, const char *message)
{
    if (actual == expected) return;

    std::fprintf(stderr, "[FAIL] %s: actual=%lld expected=%lld\n",
                 message, static_cast<long long>(actual),
                 static_cast<long long>(expected));
    ++g_failures;
}

} // namespace

int main()
{
    AVFormatContext format{};
    format.start_time = 5'000'000;

    AVStream stream{};
    stream.time_base = AVRational{1, 90'000};
    stream.start_time = 6'400'000LL * 90'000 / 1'000'000;

    avplayer::MediaTimeline timeline(&format);

    // 优先减去流自身的起始时间
    checkEqual(timeline.normalize(&stream, 8'400'000), 2'000'000,
               "stream start time");

    // 流没有起始时间时回退到容器起始时间
    AVStream noStreamStart{};
    noStreamStart.time_base = AVRational{1, 1'000};
    noStreamStart.start_time = AV_NOPTS_VALUE;
    checkEqual(timeline.normalize(&noStreamStart, 7'000'000), 2'000'000,
               "format start fallback");

    // 流与容器都没有起始时间
    AVFormatContext noFormatStart{};
    noFormatStart.start_time = AV_NOPTS_VALUE;
    avplayer::MediaTimeline noStartTimeline(&noFormatStart);
    checkEqual(noStartTimeline.normalize(&noStreamStart, 7'000'000),
               7'000'000, "no start time");

    // 空流与 NOPTS 时间戳原样返回
    checkEqual(timeline.normalize(nullptr, 1'234), 1'234, "null stream");
    checkEqual(timeline.normalize(&stream, AV_NOPTS_VALUE), AV_NOPTS_VALUE,
               "nopts timestamp");

    // 没有容器上下文时仍使用流起始时间
    avplayer::MediaTimeline streamOnly(nullptr);
    checkEqual(streamOnly.normalize(&stream, 8'400'000), 2'000'000,
               "stream only");

    // 起始时间为 0
    AVStream zeroStart{};
    zeroStart.time_base = AVRational{1, 1'000};
    zeroStart.start_time = 0;
    checkEqual(timeline.normalize(&zeroStart, 3'000'000), 3'000'000,
               "zero start time");

    if (g_failures > 0) {
        std::fprintf(stderr, "test_media_timeline: %d failure(s)\n",
                     g_failures);
        return 1;
    }

    std::printf("test_media_timeline: OK\n");
    return 0;
}
