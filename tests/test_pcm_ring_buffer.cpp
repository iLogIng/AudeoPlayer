#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stop_token>
#include <thread>

#include "output/PcmRingBuffer.hpp"

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

} // namespace

int main()
{
    // 容量与初始状态
    {
        avplayer::PcmRingBuffer buffer(16);
        checkSize(buffer.capacity(), 16, "initial capacity");
        checkSize(buffer.availableRead(), 0, "initial readable bytes");
        checkSize(buffer.availableWrite(), 16, "initial writable bytes");
    }

    // 容量为 0 时至少保留 1 字节
    {
        avplayer::PcmRingBuffer buffer(0);
        checkSize(buffer.capacity(), 1, "zero capacity");
    }

    // 基本写入、部分读取与剩余读取
    {
        avplayer::PcmRingBuffer buffer(16);
        std::stop_source source;
        const auto token = source.get_token();
        const uint8_t input[8] = {0, 1, 2, 3, 4, 5, 6, 7};

        check(buffer.write(input, sizeof(input), token), "basic write");
        checkSize(buffer.availableRead(), 8, "readable after write");
        checkSize(buffer.availableWrite(), 8, "writable after write");

        uint8_t output[8]{};
        checkSize(buffer.read(output, 3), 3, "partial read size");
        check(std::memcmp(output, input, 3) == 0, "partial read data");
        checkSize(buffer.availableRead(), 5, "readable after partial read");

        checkSize(buffer.read(output + 3, 5), 5, "remaining read size");
        check(std::memcmp(output, input, 8) == 0, "remaining read data");
        checkSize(buffer.read(output, 8), 0, "read empty buffer");
    }

    // 读写位置回绕
    {
        avplayer::PcmRingBuffer buffer(8);
        std::stop_source source;
        const auto token = source.get_token();

        const uint8_t first[6] = {0, 1, 2, 3, 4, 5};
        check(buffer.write(first, sizeof(first), token), "wrap first write");

        uint8_t discard[4]{};
        checkSize(buffer.read(discard, sizeof(discard)), 4, "wrap discard read");

        const uint8_t second[6] = {10, 11, 12, 13, 14, 15};
        check(buffer.write(second, sizeof(second), token), "wrap second write");
        checkSize(buffer.availableRead(), 8, "wrap readable bytes");

        uint8_t output[8]{};
        checkSize(buffer.read(output, sizeof(output)), 8, "wrap full read");
        const uint8_t expected[8] = {4, 5, 10, 11, 12, 13, 14, 15};
        check(std::memcmp(output, expected, sizeof(expected)) == 0,
              "wrap data order");
    }

    // 非法参数
    {
        avplayer::PcmRingBuffer buffer(8);
        std::stop_source source;
        const auto token = source.get_token();
        uint8_t data[16]{};

        check(!buffer.write(nullptr, 1, token), "null write data");
        check(!buffer.write(data, 9, token), "write larger than capacity");
        check(buffer.write(data, 0, token), "zero length write");
        checkSize(buffer.read(nullptr, 1), 0, "null read destination");
        checkSize(buffer.read(data, 0), 0, "zero length read");
    }

    // stop_token 已取消
    {
        avplayer::PcmRingBuffer buffer(8);
        std::stop_source source;
        source.request_stop();
        uint8_t data = 0;

        check(!buffer.write(&data, 1, source.get_token()),
              "stopped write token");
    }

    // clear 重置读写位置
    {
        avplayer::PcmRingBuffer buffer(8);
        std::stop_source source;
        const auto token = source.get_token();
        const uint8_t data[4] = {1, 2, 3, 4};

        check(buffer.write(data, sizeof(data), token), "clear write");
        uint8_t output[4]{};
        checkSize(buffer.read(output, sizeof(output)), 4, "clear read");
        buffer.clear();
        checkSize(buffer.availableRead(), 0, "clear readable bytes");
        checkSize(buffer.availableWrite(), 8, "clear writable bytes");
    }

    // abort 唤醒阻塞的生产者
    {
        avplayer::PcmRingBuffer buffer(4);
        std::stop_source source;
        const auto token = source.get_token();
        uint8_t data = 0;

        check(buffer.write(&data, 1, token), "fill before abort");

        std::atomic<bool> writeResult{true};
        std::thread writer([&] {
            const uint8_t value[4] = {1, 2, 3, 4};
            writeResult.store(buffer.write(value, sizeof(value), token));
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        buffer.abort();
        writer.join();

        check(!writeResult.load(), "abort unblocks writer");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "test_pcm_ring_buffer: %d failure(s)\n", g_failures);
        return 1;
    }

    std::printf("test_pcm_ring_buffer: OK\n");
    return 0;
}
