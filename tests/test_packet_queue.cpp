#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "FFmpegRAII.hpp"
#include "core/PacketQueue.hpp"

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

void checkStatus(avplayer::QueueStatus actual, avplayer::QueueStatus expected,
                 const char *message)
{
    if (actual == expected) return;

    std::fprintf(stderr, "[FAIL] %s: actual=%d expected=%d\n",
                 message, static_cast<int>(actual), static_cast<int>(expected));
    ++g_failures;
}

avplayer::ffmpeg::PacketPtr makePacket(uint8_t id, int size)
{
    auto packet = avplayer::ffmpeg::makePacket();
    if (av_new_packet(packet.get(), size) < 0) {
        std::fprintf(stderr, "[FAIL] can not allocate packet\n");
        std::abort();
    }

    if (size > 0)
        packet->data[0] = id;

    return packet;
}

} // namespace

int main()
{
    // FIFO 顺序与计数
    {
        avplayer::PacketQueue queue(8, 1024);
        std::stop_source source;
        const auto token = source.get_token();

        checkStatus(queue.push(makePacket(1, 10), token),
                    avplayer::QueueStatus::Ok, "push first packet");
        checkStatus(queue.push(makePacket(2, 10), token),
                    avplayer::QueueStatus::Ok, "push second packet");
        checkSize(queue.packetCount(), 2, "packet count after push");
        checkSize(queue.byteCount(), 20, "byte count after push");

        avplayer::ffmpeg::PacketPtr packet;
        checkStatus(queue.pop(packet, token),
                    avplayer::QueueStatus::Ok, "pop first packet");
        check(packet && packet->size == 10 && packet->data[0] == 1,
              "first packet data");

        checkStatus(queue.pop(packet, token),
                    avplayer::QueueStatus::Ok, "pop second packet");
        check(packet && packet->size == 10 && packet->data[0] == 2,
              "second packet data");
        checkSize(queue.packetCount(), 0, "packet count after pop");
        checkSize(queue.byteCount(), 0, "byte count after pop");
    }

    // EOF 之前仍可取出剩余数据
    {
        avplayer::PacketQueue queue;
        std::stop_source source;
        const auto token = source.get_token();

        checkStatus(queue.push(makePacket(7, 5), token),
                    avplayer::QueueStatus::Ok, "push before eof");
        queue.pushEof();

        avplayer::ffmpeg::PacketPtr packet;
        checkStatus(queue.pop(packet, token),
                    avplayer::QueueStatus::Ok, "pop remaining after eof");
        check(packet && packet->data[0] == 7, "remaining packet data");
        checkStatus(queue.pop(packet, token),
                    avplayer::QueueStatus::Eof, "pop at eof");
        checkStatus(queue.pop(packet, token),
                    avplayer::QueueStatus::Eof, "pop at eof again");
        checkStatus(queue.push(makePacket(8, 5), token),
                    avplayer::QueueStatus::Stopped, "push after eof");
    }

    // flush 清空数据并清除 EOF
    {
        avplayer::PacketQueue queue;
        std::stop_source source;
        const auto token = source.get_token();

        checkStatus(queue.push(makePacket(3, 5), token),
                    avplayer::QueueStatus::Ok, "push before flush");
        queue.pushEof();
        queue.flush();
        checkSize(queue.packetCount(), 0, "packet count after flush");
        checkSize(queue.byteCount(), 0, "byte count after flush");

        checkStatus(queue.push(makePacket(4, 5), token),
                    avplayer::QueueStatus::Ok, "push after flush");
        avplayer::ffmpeg::PacketPtr packet;
        checkStatus(queue.pop(packet, token),
                    avplayer::QueueStatus::Ok, "pop after flush");
        check(packet && packet->data[0] == 4, "packet data after flush");
    }

    // abort 与 reset
    {
        avplayer::PacketQueue queue;
        std::stop_source source;
        const auto token = source.get_token();

        checkStatus(queue.push(makePacket(9, 5), token),
                    avplayer::QueueStatus::Ok, "push before abort");
        queue.abort();

        avplayer::ffmpeg::PacketPtr packet;
        checkStatus(queue.pop(packet, token),
                    avplayer::QueueStatus::Stopped, "pop after abort");
        checkStatus(queue.push(makePacket(9, 5), token),
                    avplayer::QueueStatus::Stopped, "push after abort");
        checkSize(queue.packetCount(), 1, "abort keeps queued packets");

        queue.reset();
        checkSize(queue.packetCount(), 0, "packet count after reset");
        checkStatus(queue.push(makePacket(10, 5), token),
                    avplayer::QueueStatus::Ok, "push after reset");
        checkStatus(queue.pop(packet, token),
                    avplayer::QueueStatus::Ok, "pop after reset");
        check(packet && packet->data[0] == 10, "packet data after reset");
    }

    // 已取消的 stop_token
    {
        avplayer::PacketQueue queue;
        std::stop_source source;
        source.request_stop();

        avplayer::ffmpeg::PacketPtr packet;
        checkStatus(queue.push(makePacket(1, 5), source.get_token()),
                    avplayer::QueueStatus::Stopped, "stopped push token");
        checkStatus(queue.pop(packet, source.get_token()),
                    avplayer::QueueStatus::Stopped, "stopped pop token");
    }

    // abort 唤醒阻塞的 pop
    {
        avplayer::PacketQueue queue;
        std::stop_source source;
        std::atomic<avplayer::QueueStatus> status{avplayer::QueueStatus::Ok};

        std::thread consumer([&] {
            avplayer::ffmpeg::PacketPtr packet;
            status.store(queue.pop(packet, source.get_token()));
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        queue.abort();
        consumer.join();

        checkStatus(status.load(), avplayer::QueueStatus::Stopped,
                    "abort unblocks pop");
    }

    // abort 唤醒阻塞的 push
    {
        avplayer::PacketQueue queue(1, 1024);
        std::stop_source source;
        const auto token = source.get_token();

        checkStatus(queue.push(makePacket(1, 10), token),
                    avplayer::QueueStatus::Ok, "fill queue for push block");

        std::atomic<avplayer::QueueStatus> status{avplayer::QueueStatus::Ok};
        std::thread producer([&] {
            status.store(queue.push(makePacket(2, 10), token));
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        queue.abort();
        producer.join();

        checkStatus(status.load(), avplayer::QueueStatus::Stopped,
                    "abort unblocks push");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "test_packet_queue: %d failure(s)\n", g_failures);
        return 1;
    }

    std::printf("test_packet_queue: OK\n");
    return 0;
}
