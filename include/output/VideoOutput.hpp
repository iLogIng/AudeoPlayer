#pragma once

#include <SDL3/SDL_render.h>

#include "FFmpegRAII.hpp"

namespace avplayer
{

// 负责把 YUV420P 帧上传到 SDL 纹理并渲染
// 只在主线程调用
class VideoOutput
{
public:
    explicit VideoOutput(SDL_Renderer *renderer);
    ~VideoOutput();

    VideoOutput(const VideoOutput&)            = delete;
    VideoOutput& operator=(const VideoOutput&) = delete;

    bool present(const AVFrame *yuv420pFrame);
    void close();

private:
    bool ensureTexture(int width, int height);

    SDL_Renderer *m_renderer = nullptr;
    SDL_Texture *m_texture = nullptr;
    int m_textureWidth = 0;
    int m_textureHeight = 0;

}; // class VideoOutput

} // namespace avplayer
