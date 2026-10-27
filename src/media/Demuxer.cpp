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
    return av_read_frame(m_fmtctx.get(), pkt);
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
// 流中时间戳
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
