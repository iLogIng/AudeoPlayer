#include "media/Demuxer.hpp"

#include <stdexcept>
#include <string>

#include "Log.hpp"

namespace avplayer
{

void Demuxer::open(const std::filesystem::path& path)
{
    // 保证上下文干净
    close();

    // 音视频封装格式上下文
    AVFormatContext *fmtctx = nullptr;
    const std::string filename = path.string();
    // 输入文件 打开格式上下文
    if (avformat_open_input(&fmtctx, filename.c_str(), nullptr, nullptr) < 0) {
        if (fmtctx)
            avformat_close_input(&fmtctx);
        logging::FAILED_LOG("Demuxer::open", "can not open input file");
        throw std::runtime_error("demuxer: can not open input file");
    }
    m_fmtctx.reset(fmtctx);

    // 获取音视频流信息
    if (avformat_find_stream_info(m_fmtctx.get(), nullptr) < 0) {
        logging::FAILED_LOG("Demuxer::open", "can not find stream info");
        throw std::runtime_error("demuxer: can not find stream info");
    }

    // 分别查找音频流和视频流
    m_audioStreamIdx = av_find_best_stream(
        m_fmtctx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    m_videoStreamIdx = av_find_best_stream(
        m_fmtctx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    // 至少需要存在一条音视频流
    if (m_audioStreamIdx < 0 && m_videoStreamIdx < 0) {
        logging::FAILED_LOG("Demuxer::open", "can not find audio or video stream");
        throw std::runtime_error("demuxer: can not find audio or video stream");
    }
}

// 读取数据包
int Demuxer::readPacket(AVPacket *pkt)
{
    if (!m_fmtctx)
        return AVERROR_EOF;
    // 无法保证读出确定的音频流/视频流
    return av_read_frame(m_fmtctx.get(), pkt);
}

// seek 到目标时间点 单位微秒
// targetUs 使用归一化后的时间线 需要加上主时钟对应流的起始时间
int Demuxer::seekTo(int64_t targetUs)
{
    if (!m_fmtctx)
        return AVERROR_EOF;

    const AVStream *reference = audioStream();
    if (!reference)
        reference = videoStream();

    int64_t baseUs = 0;
    if (reference && reference->start_time != AV_NOPTS_VALUE) {
        baseUs = av_rescale_q(
            reference->start_time, reference->time_base, AV_TIME_BASE_Q);
    } else if (m_fmtctx->start_time != AV_NOPTS_VALUE) {
        baseUs = m_fmtctx->start_time;
    }

    return avformat_seek_file(
        m_fmtctx.get(), -1,
        INT64_MIN, targetUs + baseUs, INT64_MAX, 0);
}

// 媒体总时长 单位微秒
int64_t Demuxer::durationUs() const
{
    if (!m_fmtctx || m_fmtctx->duration == AV_NOPTS_VALUE)
        return 0;
    return m_fmtctx->duration;
}

// 关闭上下文
void Demuxer::close()
{
    m_fmtctx.reset();
    m_audioStreamIdx = -1;
    m_videoStreamIdx = -1;
}

// 音频数据流
const AVStream *Demuxer::audioStream() const
{
    if (!m_fmtctx || m_audioStreamIdx < 0)
        return nullptr;
    return m_fmtctx->streams[m_audioStreamIdx];
}
// 音频流中时间戳
AVRational Demuxer::audioTimeBase() const
{
    const AVStream *stream = audioStream();
    if (!stream)
        return AVRational{0, 1};
    return stream->time_base;
}

// 视频数据流
const AVStream *Demuxer::videoStream() const
{
    if (!m_fmtctx || m_videoStreamIdx < 0)
        return nullptr;
    return m_fmtctx->streams[m_videoStreamIdx];
}

// 视频流中时间戳
AVRational Demuxer::videoTimeBase() const
{
    const AVStream *stream = videoStream();
    if (!stream)
        return AVRational{0, 1};
    return stream->time_base;
}

} // namespace avplayer
