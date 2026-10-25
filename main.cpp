#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include "Window.hpp"
#include "Demuxer.hpp"

#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <file path>\n";
        return -1;
    }
    // 文件路径
    const fs::path filepath(argv[1]);

    // 初始化窗口
    avplayer::Window sdl_window(avplayer::WindowConfig{});
    avplayer::Demuxer demuxer(filepath);
    demuxer.open(filepath);
    // demuxer.start();

    const int FPS = 24;
    float pre_tick = 0.f;
    float gap_tick = 0.f;

    // 简单的事件循环
    SDL_Event event;
    while(true) {
        const double now_tick = static_cast<double>(SDL_GetTicks()) / 1000.00;

        // std::cerr << now_tick << std::endl;

        gap_tick = now_tick - pre_tick;
        pre_tick = now_tick;

        sdl_window.pollEvents();
        if (sdl_window.shouldClose()) {
            break;
        }
        if (SDL_GetAudioStreamQueued(demuxer.sdlAustream()) <= 0) {
            break;
        }

        float r = 0.5f + 0.5f * static_cast<float>(SDL_sin(now_tick));
        float g = 0.5f + 0.5f * static_cast<float>(SDL_sin(now_tick + SDL_PI_D * 2 / 3));
        float b = 0.5f + 0.5f * static_cast<float>(SDL_sin(now_tick + SDL_PI_D * 4 / 3));
        SDL_SetRenderDrawColorFloat(sdl_window.renderer(), r, g, b, SDL_ALPHA_OPAQUE_FLOAT);

        SDL_RenderClear(sdl_window.renderer());
        SDL_RenderPresent(sdl_window.renderer());

        SDL_Delay(1000 / FPS);
    }

    return 0;
}

