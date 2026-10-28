#include <SDL3/SDL_hints.h>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "app/PlaybackController.hpp"
#include "core/AppEvent.hpp"
#include "core/SdlContext.hpp"
#include "core/SignalListener.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    // 必须在 SDL_Init 和任何线程创建之前阻塞信号
    if (!avplayer::SignalListener::blockSignals()) {
        std::cerr << "fatal: can not block signals\n";
        return -1;
    }

    // 播放列表
    std::vector<fs::path> playlist;
    bool noWindow = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);

        if (arg == "--no-window") {
            noWindow = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return -1;
        } else {
            if (fs::is_directory(arg)) {
                fs::path dir(arg);
                try {
                    for (const auto& entry : fs::directory_iterator(dir)) {
                        if (entry.is_regular_file()) {
                            std::string ext = entry.path().extension();
                            std::transform(ext.begin(), ext.end(), ext.begin(),
                                    [](unsigned char c) { return std::tolower(c); });
                            if (ext == ".mp3" ||
                                ext == ".mp4" ||
                                ext == ".flac" ||
                                ext == ".wav")
                            {
                                playlist.emplace_back(entry.path());
                            }
                        }
                    }
                }
                catch (const fs::filesystem_error& e) {
                    std::cerr << "failed: directory traveling, " << e.what() << "\n";
                }
            }
            else {
                playlist.emplace_back(fs::absolute(arg));
            }
        }
    }

    if (playlist.empty()) {
        std::cerr << "Usage: " << argv[0]
                  << " <file/directory> [<file/directory> ...] [--no-window]\n";
        return -1;
    }

    // 使用 signalfd 监听信号 禁止 SDL 安装自己的信号处理器
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    try {
        // 初始化SDL上下文
        avplayer::SdlContext sdl(SDL_INIT_AUDIO | SDL_INIT_EVENTS);

        // 初始化应用事件循环
        if (!avplayer::AppEvent::init()) {
            std::cerr << "fatal: can not register app events\n";
            return -1;
        }

        // 操作系统信号监听
        avplayer::SignalListener signalListener;
        if (!signalListener.start()) {
            std::cerr << "fatal: can not start signal listener\n";
            return -1;
        }

        // 播放控制器
        avplayer::PlaybackController controller(
            std::move(playlist), noWindow, sdl);

        const int exitCode = controller.run();
        signalListener.stop();
        return exitCode;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return -1;
    }
}
