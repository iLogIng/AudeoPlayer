#include "output/VideoOutput.hpp"

#include "Log.hpp"

namespace avplayer
{

VideoOutput::VideoOutput(SDL_Renderer *renderer)
    : m_renderer(renderer)
{
}

VideoOutput::~VideoOutput()
{
    close();
}

// 确保纹理映射
bool VideoOutput::ensureTexture(int width, int height)
{
    if (m_texture && width == m_textureWidth && height == m_textureHeight)
        return true;

    close();

    // 创建YUV纹理
    m_texture = SDL_CreateTexture(
        m_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!m_texture) {
        logging::FAILED_LOG("VideoOutput::ensureTexture", SDL_GetError());
        return false;
    }

    m_textureWidth = width;
    m_textureHeight = height;

    if (!SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_LINEAR)) {
        logging::ERROR_LOG("VideoOutput::ensureTexture", SDL_GetError());
    }
    return true;
}

// 发送待渲染帧
bool VideoOutput::present(const AVFrame *frame)
{
    if (!m_renderer || !frame) return false;
    if (frame->format != AV_PIX_FMT_YUV420P) return false;
    if (frame->width <= 0 || frame->height <= 0) return false;

    if (!ensureTexture(frame->width, frame->height))
        return false;

    // 更新SDL纹理
    if (!SDL_UpdateYUVTexture(
            m_texture, nullptr,
            frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2])) {
        logging::ERROR_LOG("VideoOutput::present", SDL_GetError());
        return false;
    }

    // 输出宽高
    int outputWidth = 0;
    int outputHeight = 0;
    // 设置渲染输出尺寸
    if (!SDL_GetCurrentRenderOutputSize(m_renderer, &outputWidth, &outputHeight)
        || outputWidth <= 0 || outputHeight <= 0) {
        logging::ERROR_LOG("VideoOutput::present", SDL_GetError());
        return false;
    }

    // 宽高比
    const double srcAspect = static_cast<double>(frame->width) / frame->height;
    const double dstAspect = static_cast<double>(outputWidth) / outputHeight;

    // SDL矩形
    SDL_FRect dst{};
    if (dstAspect > srcAspect) {
        dst.h = static_cast<float>(outputHeight);
        dst.w = static_cast<float>(outputHeight * srcAspect);
        dst.x = static_cast<float>((outputWidth - dst.w) / 2.0);
        dst.y = 0.0f;
    } else {
        dst.w = static_cast<float>(outputWidth);
        dst.h = static_cast<float>(outputWidth / srcAspect);
        dst.x = 0.0f;
        dst.y = static_cast<float>((outputHeight - dst.h) / 2.0);
    }

    // 填充渲染颜色
    if (!SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255)
        || !SDL_RenderClear(m_renderer)
        || !SDL_RenderTexture(m_renderer, m_texture, nullptr, &dst)
        // 发送已渲染帧
        || !SDL_RenderPresent(m_renderer)) {
        logging::ERROR_LOG("VideoOutput::present", SDL_GetError());
        return false;
    }

    return true;
}

void VideoOutput::close()
{
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    m_textureWidth = 0;
    m_textureHeight = 0;
}

} // namespace avplayer
