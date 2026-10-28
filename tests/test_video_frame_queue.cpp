#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <utility>

#include "FFmpegRAII.hpp"
#include "media/VideoFrameQueue.hpp"

namespace
{

int g_failures = 0;

void check(bool condition, const char *message)
{
    if (condition) return;

    std::fprintf(stderr, "[FAIL] %s\n", message);
    ++g_failures;
}

void checkSize(size_t actual, size_t expected, const char *message)
{
    if (actual == expected) return;

    std::fprintf(stderr, "[FAIL] %s: actual=%zu expected=%zu\n",
                 message, actual, expected);
    ++g_failures;
}

void checkPts(int64_t actual, int64_t expected, const char *message)
{
    if (actual == expected) return;

    std::fprintf(stderr, "[FAIL] %s: actual=%lld expected=%lld\n",
                 message, static_cast<long long>(actual),
                 static_cast<long long>(expected));
    ++g_failures;
}

avplayer::VideoFrame makeFrame(int64_t ptsUs, size_t bytes)
{
    avplayer::VideoFrame frame;
    frame.frame = avplayer::ffmpeg::makeFrame();
    frame.ptsUs = ptsUs;
    frame.bytes = bytes;
    return frame;
}

} // namespace

int main()
{
    // 入队、查看和出队顺序
    {
        avplayer::VideoFrameQueue queue(4, 1000);
        std::stop_source source;
        const auto token = source.get_token();

        check(queue.push(makeFrame(1000, 10), token), "push first frame");
        check(queue.push(makeFrame(2000, 20), token), "push second frame");
        checkSize(queue.size(), 2, "size after push");
        check(!queue.empty(), "queue not empty");

        int64_t ptsUs = 0;
        check(queue.tryPeekPtsUs(ptsUs), "peek first frame");
        checkPts(ptsUs, 1000, "peek first pts");

        avplayer::VideoFrame frame;
        check(queue.tryPop(frame), "pop first frame");
        checkPts(frame.ptsUs, 1000, "first frame pts");
        checkSize(frame.bytes, 10, "first frame bytes");
        check(frame.frame != nullptr, "first frame handle");

        check(queue.tryPeekPtsUs(ptsUs), "peek second frame");
        checkPts(ptsUs, 2000, "peek second pts");
        check(queue.tryPop(frame), "pop second frame");
        checkPts(frame.ptsUs, 2000, "second frame pts");

        check(queue.empty(), "queue empty");
        check(!queue.tryPop(frame), "pop empty queue");
        check(!queue.tryPeekPtsUs(ptsUs), "peek empty queue");
    }

    // 空帧拒绝入队
    {
        avplayer::VideoFrameQueue queue;
        std::stop_source source;

        avplayer::VideoFrame empty;
        check(!queue.push(std::move(empty), source.get_token()),
              "reject empty frame");
    }

    // 帧数上限 消费者取出后解除阻塞
    {
        avplayer::VideoFrameQueue queue(1, 1000);
        std::stop_source source;
        const auto token = source.get_token();

        check(queue.push(makeFrame(1, 10), token), "fill frame queue");

        std::atomic<bool> pushResult{false};
        std::thread producer([&] {
            pushResult.store(queue.push(makeFrame(2, 10), token));
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check(!pushResult.load(), "push blocks when full");

        avplayer::VideoFrame frame;
        check(queue.tryPop(frame), "pop blocked frame");
        producer.join();

        check(pushResult.load(), "pop unblocks producer");
        check(queue.tryPop(frame), "pop unblocked frame");
        checkPts(frame.ptsUs, 2, "unblocked frame pts");
    }

    // 空队列允许接收超过字节上限的单帧
    {
        avplayer::VideoFrameQueue queue(4, 8);
        std::stop_source source;

        check(queue.push(makeFrame(1, 100), source.get_token()),
              "oversized frame accepted when empty");
        checkSize(queue.size(), 1, "oversized frame size");

        avplayer::VideoFrame frame;
        check(queue.tryPop(frame), "pop oversized frame");
        checkSize(frame.bytes, 100, "oversized frame bytes");
    }

    // flush 保留可用状态 reset 恢复中止状态
    {
        avplayer::VideoFrameQueue queue;
        std::stop_source source;
        const auto token = source.get_token();

        check(queue.push(makeFrame(1, 10), token), "push before flush");
        queue.flush();
        check(queue.empty(), "queue empty after flush");
        check(queue.push(makeFrame(2, 10), token), "push after flush");

        queue.abort();
        check(!queue.push(makeFrame(3, 10), token), "push after abort");

        queue.reset();
        check(queue.push(makeFrame(4, 10), token), "push after reset");

        avplayer::VideoFrame frame;
        check(queue.tryPop(frame), "pop after reset");
        checkPts(frame.ptsUs, 4, "reset frame pts");
    }

    // 已取消的 stop_token
    {
        avplayer::VideoFrameQueue queue;
        std::stop_source source;
        source.request_stop();

        check(!queue.push(makeFrame(1, 10), source.get_token()),
              "stopped push token");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "test_video_frame_queue: %d failure(s)\n",
                     g_failures);
        return 1;
    }

    std::printf("test_video_frame_queue: OK\n");
    return 0;
}
