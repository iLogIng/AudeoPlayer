#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <string>

namespace avplayer
{

struct WindowConfig
{
    int             width     = 1280;
    int             height    = 720;
    std::string     title     = "AudeoPlayer";
    SDL_WindowFlags sdl_flags = SDL_WINDOW_RESIZABLE;
}; // struct WindowConfig

// 只负责窗口与渲染器，SDL 全局初始化由 SdlContext 负责
class Window
{
public:
    explicit Window(const WindowConfig& cfg);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const { return !m_running; }
    // 处理窗口相关事件 返回是否仍运行
    bool processEvent(const SDL_Event& event);
    void requestClose() { m_running = false; }

    int width() const;
    int height() const;

    SDL_Window *window() const { return m_window; }
    SDL_Renderer *renderer() const { return m_renderer; }

private:
    SDL_Window      *m_window   = nullptr;
    SDL_Renderer    *m_renderer = nullptr;
    bool             m_running  = true;

}; // class Window

} // namespace avplayer
