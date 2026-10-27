#include "output/Window.hpp"

#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

Window::Window(const WindowConfig& cfg)
{
    m_window = SDL_CreateWindow(
        cfg.title.c_str(),
        cfg.width, cfg.height,
        cfg.sdl_flags);
    if (!m_window) {
        logging::FAILED_LOG("Window::Window", SDL_GetError());
        throw std::runtime_error("window: failed to create SDL window");
    }

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) {
        logging::FAILED_LOG("Window::Window", SDL_GetError());
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        throw std::runtime_error("window: failed to create SDL renderer");
    }
}

Window::~Window()
{
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
}

bool Window::processEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_QUIT) {
        m_running = false;
    } else if (event.type == SDL_EVENT_KEY_DOWN
            && event.key.key == SDLK_ESCAPE) {
        m_running = false;
    }
    return m_running;
}

int Window::width() const
{
    int w = 0;
    if (m_window)
        SDL_GetWindowSize(m_window, &w, nullptr);
    return w;
}

int Window::height() const
{
    int h = 0;
    if (m_window)
        SDL_GetWindowSize(m_window, nullptr, &h);
    return h;
}

} // namespace avplayer
