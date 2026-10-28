#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "core/AppEvent.hpp"
#include "core/SdlContext.hpp"
#include "core/SignalListener.hpp"
#include "output/Window.hpp"
#include "player/MediaPlayer.hpp"
#include "player/PlayerEvent.hpp"
#include "player/VideoPresenter.hpp"

int main(int argc, char* argv[])
{
    // 必须在 SDL_Init 和任何线程创建之前阻塞信号
    if (!avplayer::SignalListener::blockSignals()) {
        std::cerr << "fatal: can not block signals\n";
        return -1;
    }

    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <file path> [--no-window]\n";
        return -1;
    }

    const std::filesystem::path filepath(argv[1]);

    bool noWindow = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-window")
            noWindow = true;
    }

    // 使用 signalfd 监听信号 禁止 SDL 安装自己的信号处理器
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    try {
        // SDL 上下文
        avplayer::SdlContext sdl(SDL_INIT_AUDIO | SDL_INIT_EVENTS);

        if (!avplayer::AppEvent::init()) {
            std::cerr << "fatal: can not register app events\n";
            return -1;
        }

        // 监听系统信号
        avplayer::SignalListener signalListener;
        if (!signalListener.start()) {
            std::cerr << "fatal: can not start signal listener\n";
            return -1;
        }

        // 保证 player 比 window 先析构
        std::unique_ptr<avplayer::Window> window;

        avplayer::MediaPlayer player(filepath);
        player.open();

        if (player.hasVideo() && !noWindow) {
            sdl.initSubSystem(SDL_INIT_VIDEO);
            window = std::make_unique<avplayer::Window>(avplayer::WindowConfig{});
        }

        // 设置播放器事件回调
        player.setEventCallback(
            [](avplayer::PlayerEvent event) {
                switch (event) {
                case avplayer::PlayerEvent::ClockStarted:
                    avplayer::AppEvent::push(avplayer::AppEventCode::ClockStarted);
                    break;
                case avplayer::PlayerEvent::PlaybackFinished:
                    avplayer::AppEvent::push(avplayer::AppEventCode::PlaybackFinished);
                    break;
                case avplayer::PlayerEvent::Error:
                    avplayer::AppEvent::push(avplayer::AppEventCode::PlayerError);
                    break;
                }
            });
        player.start(window.get());

        avplayer::VideoPresenter presenter(player);

        bool running = true;
        while (running) {
            SDL_Event event;
            if (!SDL_WaitEvent(&event)) {
                std::cerr << "fatal: SDL_WaitEvent failed: " << SDL_GetError() << "\n";
                break;
            }

            if (window && !window->processEvent(event))
                running = false;

            // SDL 事件
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN
                    && event.key.key == SDLK_ESCAPE) {
                running = false;
            }

            // 软件事件
            if (avplayer::AppEvent::isAppEvent(event)) {
                switch (avplayer::AppEvent::code(event)) {
                case avplayer::AppEventCode::Quit:
                case avplayer::AppEventCode::PlayerError:
                case avplayer::AppEventCode::PlaybackFinished:
                    running = false;
                    break;

                case avplayer::AppEventCode::ClockStarted:
                    presenter.onClockStarted();
                    break;

                case avplayer::AppEventCode::VideoFrameReady:
                    presenter.onFrameReady();
                    break;

                case avplayer::AppEventCode::FrameDue:
                    presenter.onFrameDue();
                    break;
                }
            }

            if (player.error())
                running = false;
        }

        presenter.cancel();
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
