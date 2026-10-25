#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <stdexcept>
#include <string>

#include "Log.hpp"

namespace avplayer
{

struct WindowConfig
{
    int             width     = 1280;
    int             height    = 720;
    std::string     title     = "AudeoPlayer";
    SDL_WindowFlags sdl_flags = SDL_WINDOW_RESIZABLE;
}; // struct WindowConfig

class Window
{
public:
    explicit Window(const WindowConfig& cfg)
        : m_running(true)
    {
        // 初始化 音频流与视频流
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
            avplayer::logging::FAILED_LOG("SDL_Init", SDL_GetError());
            throw std::runtime_error("window: failed to init SDL Audio&Video window");
        }
        // 创建窗口
        m_window = SDL_CreateWindow(
            cfg.title.c_str(),
            cfg.width, cfg.height,
            cfg.sdl_flags
        );
        if (m_window == nullptr) {
            avplayer::logging::FAILED_LOG("SDL_CreateWindow", SDL_GetError());
            SDL_Quit();
            throw std::runtime_error("window: failed to create SDL window");
        }
        // 为窗口创建渲染器
        m_renderer = SDL_CreateRenderer(m_window, nullptr);
        if (m_renderer == nullptr) {
            avplayer::logging::FAILED_LOG("SDL_CreateRenderer", SDL_GetError());
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            throw std::runtime_error("window: failed to create SDL renderer");
        }
    }
    ~Window() {
        SDL_DestroyRenderer(m_renderer);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
    }

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const {
        return !m_running;
    }
    void pollEvents() {
        while (SDL_PollEvent(&m_event)) {
            if (m_event.type == SDL_EVENT_QUIT)
                m_running = false;
            else if (m_event.type == SDL_EVENT_KEY_DOWN && m_event.key.key == SDLK_ESCAPE) {
                m_running = false;
            }
        }
    }
    void requestClose() {

    }

    int  width() const;
    int  height() const;

    SDL_Window *window() const { return m_window; }
    SDL_Renderer *renderer() const { return m_renderer; }

private:
    SDL_Window      *m_window;
    SDL_Renderer    *m_renderer;
    SDL_Event       m_event;
    bool            m_running;
}; // class Window

} // namespace avplayer

