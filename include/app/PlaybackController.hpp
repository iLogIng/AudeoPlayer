#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/TerminalInput.hpp"

namespace avplayer
{

class SdlContext;
class Window;
class MediaPlayer;
class VideoPresenter;

// 播放列表与交互控制
class PlaybackController
{
public:
    PlaybackController(std::vector<std::filesystem::path> playlist,
                       bool noWindow,
                       SdlContext& sdl);
    ~PlaybackController();

    PlaybackController(const PlaybackController&)            = delete;
    PlaybackController& operator=(const PlaybackController&) = delete;

    int run();

private:
    void playIndex(size_t index);
    void stopCurrent();
    void next();
    void previous();
    void togglePause();
    void adjustVolume(float delta);
    void seekRelative(int64_t deltaUs);

    // 事件处理
    void handleKey(SDL_Keycode key, bool repeat);
    void handleAppEvent(const SDL_Event& event);
    void handleTerminal(TerminalCommand command);
    void handlePlayerError();

    void installPlayerCallback(MediaPlayer& player, uint64_t sessionId);
    void onTrackFailed();
    void reportError(const std::string& message);

    std::vector<std::filesystem::path> m_playlist;
    size_t m_index = 0;
    bool m_noWindow = false;
    SdlContext& m_sdl;

    std::unique_ptr<Window> m_window;
    std::unique_ptr<MediaPlayer> m_player;
    std::unique_ptr<VideoPresenter> m_presenter;
    std::unique_ptr<TerminalInput> m_terminal;

    float m_volume = 1.0f;
    bool m_paused = false;
    bool m_running = true;
    int m_exitCode = 0;

    uint64_t m_sessionId = 0;
    uint64_t m_nextSessionId = 1;
    size_t m_consecutiveFailures = 0;

}; // class PlaybackController

} // namespace avplayer
