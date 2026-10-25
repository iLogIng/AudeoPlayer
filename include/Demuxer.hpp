#pragma once

#include <SDL3/SDL_audio.h>
#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <stdexcept>

#include "Thread.hpp"
#include "Log.hpp"

namespace fs = std::filesystem;

namespace avplayer
{

// 音视频解封装器
class Demuxer : public Thread
{
public:
    Demuxer(const fs::path& path)
        : m_path(path) { }

    ~Demuxer() {
        if (m_fmtctx)
            avformat_close_input(&m_fmtctx);
        if (m_codectx)
            avcodec_free_context(&m_codectx);
        if (m_frame)
            av_frame_free(&m_frame);
        if (m_packet)
            av_packet_free(&m_packet);
        if (m_sdlAustream)
            SDL_DestroyAudioStream(m_sdlAustream);
        if (m_swrctx)
            swr_free(&m_swrctx);
    }

    void open(const fs::path& path) {
        // 初始化解码上下文
        if (avformat_open_input(&m_fmtctx, path.c_str(), nullptr, nullptr)) {
            logging::FAILED_LOG("Demuxer::open", "can not open input file");
            throw std::runtime_error("can not open input file");
        }
        if (avformat_find_stream_info(m_fmtctx, nullptr) < 0) {
            logging::FAILED_LOG("Demuxer::open", "can not find stream info");
            throw std::runtime_error("can not find stream info");
        }

        m_audioStreamIdx = av_find_best_stream(m_fmtctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (m_audioStreamIdx < 0) {
            logging::FAILED_LOG("Demuxer::open", "can not find the best stream");
            throw std::runtime_error("can not find the best stream");
        }

        AVStream *audio_stream = m_fmtctx->streams[m_audioStreamIdx];
        const AVCodec *codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
        AVCodecContext *m_codectx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_codectx, audio_stream->codecpar);
        if (avcodec_open2(m_codectx, codec, nullptr) < 0) {
            logging::FAILED_LOG("Demuxer::open", "can not get well codec context");
            throw std::runtime_error("can not get well codec context");
        }

        // 配置重采样器
        // 目标格式：44100Hz, 双声道立体声, 16位有符号整型交错 (S16)
        int out_sample_rate = 44100;
        AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;

        swr_alloc_set_opts2(&m_swrctx,
            &out_ch_layout, out_sample_fmt, out_sample_rate,
            &m_codectx->ch_layout, m_codectx->sample_fmt, m_codectx->sample_rate,
            0, nullptr);
        if (swr_init(m_swrctx) < 0) {
            logging::FAILED_LOG("Demuxer::open", "can not init software resample");
            std::runtime_error("can not init software resample");
        }

        SDL_AudioSpec spec = {
            .format = SDL_AUDIO_S16,    // 对应 AV_SAMPLE_FMT_S16
            .channels = 2,              // 双声道
            .freq = 44100,              // 采样率
        };

        m_sdlAustream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!m_sdlAustream) {
            logging::FAILED_LOG("Demuxer::open", "can not open audio stream");
            std::runtime_error("can not open audio stream");
        }
        SDL_ResumeAudioStreamDevice(m_sdlAustream);

        // 主解码循环
        m_packet = av_packet_alloc();
        m_frame = av_frame_alloc();
        while (av_read_frame(m_fmtctx, m_packet) >= 0) {
            if (m_packet->stream_index == m_audioStreamIdx) {
                // 发送压缩包给解码器
                if (avcodec_send_packet(m_codectx, m_packet) < 0) {
                    av_packet_unref(m_packet);
                    continue;
                }

                // 接收解码后的原始帧
                while (avcodec_receive_frame(m_codectx, m_frame) >= 0) {
                    // 计算重采样后最大可能的样本数
                    int out_max_samples = av_rescale_rnd(
                        swr_get_delay(m_swrctx, m_codectx->sample_rate) + m_frame->nb_samples,
                        out_sample_rate, m_codectx->sample_rate, AV_ROUND_UP);

                    // 分配输出缓冲区
                    uint8_t* out_buffer = nullptr;
                    av_samples_alloc(&out_buffer, nullptr, out_ch_layout.nb_channels,
                                     out_max_samples, out_sample_fmt, 0);

                    // 执行重采样转换
                    int converted_samples = swr_convert(
                        m_swrctx, &out_buffer, out_max_samples,
                        (const uint8_t**)m_frame->extended_data, m_frame->nb_samples);

                    if (converted_samples > 0) {
                        // 计算实际PCM数据的字节大小
                        int out_bytes = av_samples_get_buffer_size(
                            nullptr, out_ch_layout.nb_channels, converted_samples, out_sample_fmt, 1);

                        // 核心：推送到 SDL 音频流
                        SDL_PutAudioStreamData(m_sdlAustream, out_buffer, out_bytes);
                    }
                    av_freep(&out_buffer);
                }
            }
            av_packet_unref(m_packet);
        }

    } 

    int audioStreamIndex() const { return m_audioStreamIdx; }
    AVFormatContext *formatContext() const { return m_fmtctx; }
    AVCodecContext *codecContext() const { return m_codectx; }

    const AVChannelLayout *channelLayout() { return &m_codectx->ch_layout; }
    AVSampleFormat sampleFormat() { return m_codectx->sample_fmt; }
    int sampleRate() { return m_codectx->sample_rate; }

    SDL_AudioStream *sdlAustream() { return m_sdlAustream; }

protected:
    void onAbort() override {
        logging::INFO_LOG("Demuxer::onAbort", "on abort");
    }
    void run() override {
        logging::INFO_LOG("Demuxer::run", "demuxer running");
        open(m_path);
    }

private:

    AVFormatContext *m_fmtctx = nullptr;
    AVCodecContext *m_codectx = nullptr;
    SwrContext *m_swrctx      = nullptr;
    SDL_AudioStream *m_sdlAustream = nullptr;
    AVPacket *m_packet    = nullptr;
    AVFrame *m_frame      = nullptr;

    int m_audioStreamIdx = -1;
    fs::path m_path;

}; // class Demuxer

} // namespace avplayer

