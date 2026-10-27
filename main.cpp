#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_timer.h>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "core/AppEvent.hpp"
#include "core/SdlContext.hpp"
#include "core/SignalListener.hpp"
#include "output/Window.hpp"
#include "player/AudioPlayer.hpp"

namespace
{

// 一次性排空定时器 只推送事件，不直接操作播放器
Uint32 SDLCALL onDrainTimer(void*, SDL_TimerID, Uint32)
{
    // 队列满时稍后重试
    return avplayer::AppEvent::push(avplayer::AppEventCode::DrainTimeout) ? 0 : 10;
}

} // namespace

int main(int argc, char* argv[])
{
    // 必须在 SDL_Init 和任何线程创建之前阻塞信号
    if (!avplayer::SignalListener::blockSignals()) {
        std::cerr << "fatal: can not block signals\n";
        return -1;
    }

    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <file path> [--window]\n";
        return -1;
    }

    const std::filesystem::path filepath(argv[1]);

    bool showWindow = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--window")
            showWindow = true;
    }

    // 使用 signalfd 监听信号 禁止 SDL 安装自己的信号处理器
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    try {
        // 音频优先 默认不创建窗口
        SDL_InitFlags flags = SDL_INIT_AUDIO | SDL_INIT_EVENTS;
        if (showWindow) flags |= SDL_INIT_VIDEO;

        avplayer::SdlContext sdl(flags);

        if (!avplayer::AppEvent::init()) {
            std::cerr << "fatal: can not register app events\n";
            return -1;
        }

        avplayer::SignalListener signalListener;
        if (!signalListener.start()) {
            std::cerr << "fatal: can not start signal listener\n";
            return -1;
        }

        std::unique_ptr<avplayer::Window> window;
        if (showWindow) {
            window = std::make_unique<avplayer::Window>(avplayer::WindowConfig{});
        }

        avplayer::AudioPlayer player(filepath);
        player.setEventCallback(
        [](avplayer::PlayerEvent event) {
            avplayer::AppEvent::push(
                event == avplayer::PlayerEvent::AudioDecodeEof
                    ? avplayer::AppEventCode::AudioDecodeEof
                    : avplayer::AppEventCode::PlayerError);
        });
        player.start();

        bool running = true;
        bool draining = false;
        SDL_TimerID drainTimer = 0;

        while (running) {
            SDL_Event event;
            if (!SDL_WaitEvent(&event)) {
                std::cerr << "fatal: SDL_WaitEvent failed: " << SDL_GetError() << "\n";
                break;
            }

            // 窗口事件
            if (window && !window->processEvent(event))
                running = false;

            // 无窗口时的退出事件
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN
                    && event.key.key == SDLK_ESCAPE) {
                running = false;
            }

            // 应用事件
            if (avplayer::AppEvent::isAppEvent(event)) {
                switch (avplayer::AppEvent::code(event)) {
                case avplayer::AppEventCode::Quit:
                case avplayer::AppEventCode::PlayerError:
                    running = false;
                    break;

                case avplayer::AppEventCode::AudioDecodeEof:
                    if (!draining) {
                        const uint32_t drainMs = player.drainDurationMs();
                        if (drainMs == 0) {
                            running = false;
                        } else {
                            draining = true;
                            drainTimer = SDL_AddTimer(drainMs, onDrainTimer, nullptr);
                            if (drainTimer == 0) running = false;
                        }
                    }
                    break;

                case avplayer::AppEventCode::DrainTimeout:
                    if (draining) running = false;
                    break;
                }
            }

            // 事件之外的检查
            if (player.error()) running = false;
        }

        if (drainTimer) {
            SDL_RemoveTimer(drainTimer);
            drainTimer = 0;
        }

        // 停止信号监听
        signalListener.stop();
        player.stop();

        if (std::exception_ptr error = player.error()) {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& e) {
                std::cerr << "playback failed: " << e.what() << "\n";
            }
            return -1;
        }
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return -1;
    }

    return 0;
}
