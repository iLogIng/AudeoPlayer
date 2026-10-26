#include "media/Resampler.hpp"

#include <SDL3/SDL_audio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/samplefmt.h>
#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

    // 将SDL_AudioFormat转换为FFmpeg::AVSampleFormat
AVSampleFormat Resampler::toAVSampleFormat(SDL_AudioFormat format)
{
    switch (format) {
        case SDL_AUDIO_U8: return AV_SAMPLE_FMT_U8;
        case SDL_AUDIO_S16: return AV_SAMPLE_FMT_S16;
        case SDL_AUDIO_S32: return AV_SAMPLE_FMT_S32;
        case SDL_AUDIO_F32: return AV_SAMPLE_FMT_FLT;
        default: return AV_SAMPLE_FMT_NONE;
    };
}

// 根据 解码器decoder SDL音频数据格式说明符SDL_AudioSpec 进行配置
void Resampler::configure(const AVCodecContext *decoder, const SDL_AudioSpec& outputSpec)
{
    if (!decoder) {
        throw std::invalid_argument("resampler: decoder context is null");
    }

    // 重采样器输出格式
    m_outFormat = toAVSampleFormat(outputSpec.format);
    if (m_outFormat == AV_SAMPLE_FMT_NONE) {
        logging::FAILED_LOG("Resampler::configure", "unsupported output sample format");
        throw std::runtime_error("resampler: unsupported output sample format");
    }

    // 输出采样率
    m_outSampleRate = outputSpec.freq;
    // 输出通道数
    m_outChannels   = outputSpec.channels;
    // 输入采样率
    m_inSampleRate  = decoder->sample_rate > 0 ? decoder->sample_rate : m_outSampleRate;

    // 音频通道输出
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, m_outChannels);

    // 软件重采样上下文
    SwrContext *swr = nullptr;
    int ret = swr_alloc_set_opts2(
        &swr,
        &outLayout, m_outFormat, m_outSampleRate,
        &decoder->ch_layout, decoder->sample_fmt, m_inSampleRate,
        0, nullptr);
    // 释放在栈上创建的outLayout变量内部可能持有的堆内存
    av_channel_layout_uninit(&outLayout);

    if (ret < 0 || !swr) {
        if (swr) swr_free(&swr);
        logging::FAILED_LOG("Resampler::configure", "can not alloc swr context");
        throw std::runtime_error("resampler: can not alloc swr context");
    }

    m_swrctx.reset(swr);
    if (swr_init(m_swrctx.get()) < 0) {
        logging::FAILED_LOG("Resampler::configure", "can not init swr context");
        throw std::runtime_error("resampler: can not init swr context");
    }
}

// 音频帧重采样转换
bool Resampler::convert(const AVFrame *frame, PcmChunk& out)
{
    if (!frame || !m_swrctx) return false;

    // 采样率
    const int inRate = frame->sample_rate > 0 ? frame->sample_rate : m_inSampleRate;
    if (inRate <= 0) return false;

    // 计算重采样后最大可能的样本数
    // (delay + nb_sample)*m_outSampleRate / inRate
    // delay 保证使用多相位滤波插值的过去与未来都能被考虑，减少延迟累计的影响
    const int outMaxSamples = static_cast<int>(av_rescale_rnd(
        swr_get_delay(m_swrctx.get(), inRate) + frame->nb_samples,
        m_outSampleRate, inRate, AV_ROUND_UP));
    if (outMaxSamples <= 0) return false;

    // 输出缓冲长度
    const int outBufferSize = av_samples_get_buffer_size(
        nullptr, m_outChannels, outMaxSamples, m_outFormat, 0);
    if (outBufferSize < 0) return false;

    out.data.resize(static_cast<size_t>(outBufferSize));
    // 输出数据视图
    uint8_t *outData[1] = { out.data.data() };
    // 输入数据视图
    const uint8_t **inData = const_cast<const uint8_t **>(frame->extended_data);

    // 执行转换
    const int converted = swr_convert(
        m_swrctx.get(),
        outData, outMaxSamples,
        inData, frame->nb_samples);
    if (converted < 0) return false;
    if (converted == 0) {
        out.data.clear();
        return false;
    }

    // 实际大小
    const int actualSize = av_samples_get_buffer_size(
        nullptr, m_outChannels, converted, m_outFormat, 1);
    if (actualSize < 0) return false;

    // PCM数据实际大小
    // 最佳时间戳
    out.data.resize(static_cast<size_t>(actualSize));
    out.pts = frame->best_effort_timestamp;
    return true;
}

// 刷新重采样器 输出内部剩余的缓冲数据
bool Resampler::flush(PcmChunk& out)
{
    if (!m_swrctx || m_inSampleRate <= 0) return false;

    // 查询采样器内部累积延迟
    const int64_t delay = swr_get_delay(m_swrctx.get(), m_inSampleRate);
    if (delay <= 0) return false;

    // 最大可能的输出采样率
    const int outMaxSamples = static_cast<int>(av_rescale_rnd(
        delay, m_outSampleRate, m_inSampleRate, AV_ROUND_UP));
    if (outMaxSamples <= 0) return false;

    // 输出缓冲上界
    const int outBufferSize = av_samples_get_buffer_size(
        nullptr, m_outChannels, outMaxSamples, m_outFormat, 1);
    if (outBufferSize < 0) return false;

    out.data.resize(static_cast<size_t>(outBufferSize));
    uint8_t *outData[1] = { out.data.data() };

    // 重采样转换
    const int converted = swr_convert(m_swrctx.get(), outData, outMaxSamples, nullptr, 0);
    if (converted <= 0) {
        out.data.clear();
        return false;
    }

    // 实际输出缓冲大小
    const int actualSize = av_samples_get_buffer_size(
        nullptr, m_outChannels, converted, m_outFormat, 1);
    if (actualSize < 0) return false;

    out.data.resize(static_cast<size_t>(actualSize));
    return true;
}

// 重置重采样器
void Resampler::reset()
{
    if (!m_swrctx) return;
    swr_close(m_swrctx.get());
    swr_init(m_swrctx.get());
}

} // namespace avplayer
