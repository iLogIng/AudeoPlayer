#include <SDL3/SDL_init.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "core/SdlContext.hpp"
#include "output/Window.hpp"
#include "player/AudioPlayer.hpp"

namespace
{

std::atomic<bool> g_quit{false};

void onSignal(int)
{
    g_quit.store(true);
}

} // namespace

int main(int argc, char* argv[])
{
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

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try {
        // 音频优先 默认不创建窗口
        SDL_InitFlags flags = SDL_INIT_AUDIO | SDL_INIT_EVENTS;
        if (showWindow) flags |= SDL_INIT_VIDEO;

        // SDL上下文
        avplayer::SdlContext sdl(flags);

        // SDL窗口
        std::unique_ptr<avplayer::Window> window;
        if (showWindow) {
            window = std::make_unique<avplayer::Window>(avplayer::WindowConfig{});
        }

        // 启动播放器
        avplayer::AudioPlayer player(filepath);
        player.start();

        while (!g_quit.load()) {
            if (window) {
                window->pollEvents();
                if (window->shouldClose()) break;
            }
            if (player.error()) break;
            if (player.finished()) break;

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // 停止播放器
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
