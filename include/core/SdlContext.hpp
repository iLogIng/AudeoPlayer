#pragma once

#include <SDL3/SDL_init.h>
#include <stdexcept>

#include "Log.hpp"

namespace avplayer
{

// SDL 全局初始化的 RAII 包装
class SdlContext
{
public:
    explicit SdlContext(SDL_InitFlags flags) {
        if (!SDL_Init(flags)) {
            logging::FAILED_LOG("SDL_Init", SDL_GetError());
            throw std::runtime_error("sdl: failed to init SDL");
        }
    }

    ~SdlContext() {
        SDL_Quit();
    }

    SdlContext(const SdlContext&)            = delete;
    SdlContext& operator=(const SdlContext&) = delete;

    void initSubSystem(SDL_InitFlags flags) {
        if (!SDL_InitSubSystem(flags)) {
            logging::FAILED_LOG("SDL_InitSubSystem", SDL_GetError());
            throw std::runtime_error("sdl: failed to init subsystem");
        }
    }

}; // class SdlContext

} // namespace avplayer
