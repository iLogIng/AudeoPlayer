#include "player/AudioPlayer.hpp"

#include <chrono>
#include <new>
#include <stdexcept>
#include <thread>

#include "Log.hpp"

namespace avplayer
{

AudioPlayer::AudioPlayer(const std::filesystem::path& path)
    : m_path(path)
{
}

AudioPlayer::~AudioPlayer()
{
    stop();
}

// 启动
void AudioPlayer::start()
{
    if (m_started.exchange(true)) return;

    try {
        // 解封装
        m_demuxer.open(m_path);

        const AVStream *stream = m_demuxer.audioStream();
        if (!stream) {
            throw std::runtime_error("audio player: no audio stream");
        }

        // 解码
        m_decoder.open(stream);
        // 打开音频输出设备
        m_output.open();
        // 重采样
        m_resampler.configure(m_decoder.context(), m_output.spec());
    } catch (...) {
        m_started.store(false);
        throw;
    }

    m_demuxThread = std::jthread([this](std::stop_token st) {
        try {
            demuxLoop(st);
        } catch (...) {
            onWorkerExit(std::current_exception());
        }
    });

    m_decodeThread = std::jthread([this](std::stop_token st) {
        try {
            decodeLoop(st);
        } catch (...) {
            onWorkerExit(std::current_exception());
        }
    });
}

void AudioPlayer::stop()
{
    m_demuxThread.request_stop();
    m_decodeThread.request_stop();
    m_packetQueue.abort();

    if (m_demuxThread.joinable()) m_demuxThread.join();
    if (m_decodeThread.joinable()) m_decodeThread.join();

    m_output.close();
    m_decoder.close();
    m_demuxer.close();
    m_started.store(false);
}

void AudioPlayer::pause(bool paused)
{
    m_output.pause(paused);
}

bool AudioPlayer::finished() const
{
    return m_decodeDone.load() && m_output.queuedBytes() == 0;
}

std::exception_ptr AudioPlayer::error() const
{
    std::lock_guard lock(m_errorMutex);
    return m_error;
}

// 解封装循环
void AudioPlayer::demuxLoop(std::stop_token st)
{
    ffmpeg::PacketPtr packet = ffmpeg::makePacket();
    if (!packet)
        throw std::bad_alloc();

    // 线程执行过程中解封装 与线程同时停止
    while (!st.stop_requested()) {
        const int ret = m_demuxer.readPacket(packet.get());

        // 暂时没有数据
        if (ret == AVERROR(EAGAIN)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        // 文件结尾
        if (ret == AVERROR_EOF) {
            m_packetQueue.pushEof();
            return;
        }
        // 错误 无法读出数据
        if (ret < 0) {
            logging::FAILED_LOG("AudioPlayer::demuxLoop", "can not read packet");
            throw std::runtime_error("audio player: can not read packet");
        }

        // 只保留音频流的数据包
        if (packet->stream_index != m_demuxer.audioStreamIndex()) {
            // 减少引用计数 清理内存块
            av_packet_unref(packet.get());
            continue;
        }

        // 解封装数据包入队
        if (m_packetQueue.push(std::move(packet), st) != QueueStatus::Ok)
            return;

        packet = ffmpeg::makePacket();
        if (!packet)
            throw std::bad_alloc();
    }
}

// 解码循环
void AudioPlayer::decodeLoop(std::stop_token st)
{
    ffmpeg::FramePtr frame = ffmpeg::makeFrame();
    if (!frame)
        throw std::bad_alloc();

    // 取出解码器当前可用的所有帧，转换后送入输出
    auto drainFrames = [&]() -> bool {
        while (!st.stop_requested()) {
            // 从解码器获取数据帧
            const int ret = m_decoder.receive(frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return true;
            if (ret < 0) {
                logging::FAILED_LOG("AudioPlayer::decodeLoop", "can not receive frame");
                throw std::runtime_error("audio player: can not receive frame");
            }

            // 使用重采样转换数据帧
            PcmChunk chunk;
            const bool converted = m_resampler.convert(frame.get(), chunk);
            av_frame_unref(frame.get());
            if (converted && !chunk.data.empty()) {
                // 推入播放设备
                if (!m_output.push(chunk.data.data(), chunk.data.size(), st))
                    return false;
            }
        }
        return false;
    };

    // 文件结束时刷新解码器与重采样器
    auto flushAll = [&]() -> bool {
        const int ret = m_decoder.send(nullptr);
        if (ret < 0 && ret != AVERROR_EOF) {
            logging::FAILED_LOG("AudioPlayer::decodeLoop", "can not flush decoder");
            throw std::runtime_error("audio player: can not flush decoder");
        }
        if (!drainFrames()) return false;

        PcmChunk chunk;
        while (m_resampler.flush(chunk)) {
            if (!m_output.push(chunk.data.data(), chunk.data.size(), st))
                return false;
            chunk = PcmChunk{};
        }
        return true;
    };

    while (!st.stop_requested()) {
        ffmpeg::PacketPtr packet;
        const QueueStatus status = m_packetQueue.pop(packet, st);

        if (status == QueueStatus::Stopped)
            return;

        // 文件结尾 刷新解码器与重采样器缓冲
        if (status == QueueStatus::Eof) {
            if (flushAll()) m_decodeDone.store(true);
            return;
        }

        // 重试次数 < 4
        int retry = 0;
        // 发送待解码数据包
        int ret = m_decoder.send(packet.get());
        while (ret == AVERROR(EAGAIN) && retry++ < 4) {
            if (!drainFrames()) return;
            ret = m_decoder.send(packet.get());
        }

        if (ret == AVERROR(EAGAIN)) {
            logging::FAILED_LOG("AudioPlayer::decodeLoop", "decoder is stuck");
            throw std::runtime_error("audio player: decoder is stuck");
        }
        if (ret < 0 && ret != AVERROR_EOF) {
            logging::FAILED_LOG("AudioPlayer::decodeLoop", "can not send packet");
            throw std::runtime_error("audio player: can not send packet");
        }

        if (!drainFrames()) return;
    }
}

// 退出工作线程
void AudioPlayer::onWorkerExit(std::exception_ptr error)
{
    if (error)
        setError(error);

    m_decodeDone.store(true);
    m_packetQueue.abort();
    // 此处仅请求停止，不能在 worker 内 join 自己
    m_demuxThread.request_stop();
    m_decodeThread.request_stop();
}

// 设置错误
void AudioPlayer::setError(std::exception_ptr error)
{
    std::lock_guard lock(m_errorMutex);
    if (!m_error) m_error = error;
}

} // namespace avplayer
