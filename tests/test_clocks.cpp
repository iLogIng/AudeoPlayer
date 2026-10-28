#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "sync/AudioClock.hpp"
#include "sync/SystemClock.hpp"

namespace
{

int g_failures = 0;

void check(bool condition, const char *message)
{
    if (condition) return;

    std::fprintf(stderr, "[FAIL] %s\n", message);
    ++g_failures;
}

void checkNear(int64_t actual, int64_t expected, int64_t tolerance,
               const char *message)
{
    const int64_t diff = actual > expected
        ? actual - expected
        : expected - actual;
    if (diff <= tolerance) return;

    std::fprintf(stderr,
                 "[FAIL] %s: actual=%lld expected=%lld tolerance=%lld\n",
                 message, static_cast<long long>(actual),
                 static_cast<long long>(expected),
                 static_cast<long long>(tolerance));
    ++g_failures;
}

void sleepMs(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

} // namespace

int main()
{
    // AudioClock 初始状态与 reset
    {
        avplayer::AudioClock clock;
        check(!clock.valid(), "audio clock initially invalid");
        check(clock.paused(), "audio clock initially paused");

        clock.reset(48000);
        check(clock.valid(), "audio clock valid after reset");
        check(clock.paused(), "audio clock paused after reset");
        checkNear(clock.positionUs(), 0, 20'000, "audio clock initial position");
    }

    // AudioClock 按消费帧数推进
    {
        avplayer::AudioClock clock;
        clock.reset(48000);
        clock.resume();
        clock.advance(48000, 0);

        checkNear(clock.positionUs(), 1'000'000, 50'000,
                  "audio clock advance one second");
    }

    // AudioClock 扣除缓冲与设备延迟
    {
        avplayer::AudioClock clock;
        clock.reset(48000);
        clock.resume();
        clock.setDeviceLatencyUs(50'000);
        clock.advance(24000, 100'000);

        checkNear(clock.positionUs(), 350'000, 50'000,
                  "audio clock buffered and latency");
    }

    // AudioClock 暂停期间位置不变
    {
        avplayer::AudioClock clock;
        clock.reset(48000);
        clock.resume();
        clock.advance(48000, 0);
        clock.pause();

        const int64_t first = clock.positionUs();
        sleepMs(60);
        const int64_t second = clock.positionUs();

        checkNear(second, first, 5'000, "audio clock paused position");
    }

    // AudioClock 恢复后位置不回退 也不保留暂停时间偏移
    {
        avplayer::AudioClock clock;
        clock.reset(48000);
        clock.resume();
        clock.advance(24000, 0);

        clock.pause();
        sleepMs(100);
        clock.resume();
        clock.advance(48000, 0);

        // 暂停前累计 0.5s 恢复后再消费 1s 位置不能因为暂停时间而回退
        const int64_t position = clock.positionUs();
        check(position >= 1'450'000,
              "audio clock position after resume");
        check(position <= 2'500'000,
              "audio clock position after resume in range");
    }

    // AudioClock 重新设置基准 PTS
    {
        avplayer::AudioClock clock;
        clock.reset(48000, 1'000'000);
        clock.resume();
        clock.advance(0, 0);
        checkNear(clock.positionUs(), 1'000'000, 20'000,
                  "audio clock base pts");

        clock.setBasePtsUs(2'000'000);
        checkNear(clock.positionUs(), 2'000'000, 20'000,
                  "audio clock reset base pts");
    }

    // SystemClock 启动与推进
    {
        avplayer::SystemClock clock;
        check(!clock.valid(), "system clock initially invalid");
        check(clock.paused(), "system clock initially paused");

        clock.start(2'000'000);
        check(clock.valid(), "system clock valid after start");
        check(!clock.paused(), "system clock running after start");
        checkNear(clock.positionUs(), 2'000'000, 20'000,
                  "system clock initial position");

        sleepMs(40);
        check(clock.positionUs() >= 2'010'000, "system clock advances");
    }

    // SystemClock 暂停与恢复
    {
        avplayer::SystemClock clock;
        clock.start(0);
        sleepMs(20);

        clock.pause();
        const int64_t first = clock.positionUs();
        sleepMs(50);
        const int64_t second = clock.positionUs();
        checkNear(second, first, 5'000, "system clock paused position");

        clock.resume();
        sleepMs(40);
        check(clock.positionUs() >= second + 10'000,
              "system clock resumes");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "test_clocks: %d failure(s)\n", g_failures);
        return 1;
    }

    std::printf("test_clocks: OK\n");
    return 0;
}
