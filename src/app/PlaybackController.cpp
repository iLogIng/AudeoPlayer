#include "app/PlaybackController.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

#include "Log.hpp"
#include "core/AppEvent.hpp"
#include "core/SdlContext.hpp"
#include "output/Window.hpp"
#include "player/MediaPlayer.hpp"
#include "player/VideoPresenter.hpp"

namespace avplayer
{

namespace
{

constexpr int64_t kSeekStepUs = 5'000'000;
constexpr float kVolumeStep = 0.05f;

std::string exceptionMessage(std::exception_ptr error)
{
    if (!error) return "unknown error";

    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
}

} // namespace

PlaybackController::PlaybackController(
    std::vector<std::filesystem::path> playlist,
    bool noWindow,
    SdlContext& sdl)
    : m_playlist(std::move(playlist))
    , m_noWindow(noWindow)
    , m_sdl(sdl)
{
}

PlaybackController::~PlaybackController()
{
    stopCurrent();

    if (m_terminal) {
        m_terminal->stop();
        m_terminal.reset();
    }

    m_window.reset();
}

int PlaybackController::run()
{
    if (m_playlist.empty())
        return -1;

    playIndex(0);

    // 没有播放窗口时使用终端按键控制
    if (m_running && (m_noWindow || !m_window)) {
        m_terminal = std::make_unique<TerminalInput>();
        if (!m_terminal->start()) {
            logging::INFO_LOG(
                "PlaybackController::run",
                "terminal controls unavailable");
            m_terminal.reset();
        } else {
            logging::INFO_LOG(
                "PlaybackController::run",
                "controls: space=pause  n=next  p=previous  +/-=volume"
                "  ,/.=seek  q=quit");
        }
    }

    // 事件循环
    while (m_running) {
        SDL_Event event;
        if (!SDL_WaitEvent(&event)) {
            reportError("SDL_WaitEvent failed");
            m_exitCode = -1;
            break;
        }

        if (event.type == SDL_EVENT_QUIT) {
            m_running = false;
            break;
        }

        if (m_window && !m_window->processEvent(event)) {
            m_running = false;
            break;
        }

        if (event.type == SDL_EVENT_KEY_DOWN)
            handleKey(event.key.key, event.key.repeat);

        if (AppEvent::isAppEvent(event))
            handleAppEvent(event);
    }

    stopCurrent();

    if (m_terminal) {
        m_terminal->stop();
        m_terminal.reset();
    }

    return m_exitCode;
}

void PlaybackController::playIndex(size_t index)
{
    if (!m_running || m_playlist.empty())
        return;

    stopCurrent();
    m_index = index;

    try {
        auto player = std::make_unique<MediaPlayer>(m_playlist[index]);
        player->open();

        // --no-window 下只有视频流的文件没有可用的输出
        if (m_noWindow && !player->hasAudio()) {
            throw std::runtime_error("no audio stream for --no-window");
        }

        const uint64_t sessionId = m_nextSessionId++;
        m_sessionId = sessionId;
        player->setSessionId(sessionId);
        installPlayerCallback(*player, sessionId);

        if (!m_noWindow && player->hasVideo() && !m_window) {
            m_sdl.initSubSystem(SDL_INIT_VIDEO);
            m_window = std::make_unique<Window>(WindowConfig{});
        }

        player->setVolume(m_volume);
        player->start(m_window.get());

        m_player = std::move(player);
        if (m_player->videoEnabled())
            m_presenter = std::make_unique<VideoPresenter>(*m_player, sessionId);
        m_paused = false;
        m_consecutiveFailures = 0;

        const std::string message =
            "playing (" + std::to_string(m_index + 1) + "/"
            + std::to_string(m_playlist.size()) + "): "
            + m_playlist[index].string();
        logging::INFO_LOG("PlaybackController::playIndex", message.c_str());
    } catch (const std::exception& e) {
        reportError("failed to play " + m_playlist[index].string()
                    + ": " + e.what());
        onTrackFailed();
    }
}

// 发送停止信号
void PlaybackController::stopCurrent()
{
    if (m_presenter) {
        m_presenter->cancel();
        m_presenter.reset();
    }

    if (m_player) {
        m_player->stop();
        m_player.reset();
    }

    m_sessionId = 0;
}

// 下一首
void PlaybackController::next()
{
    if (m_playlist.empty() || !m_running)
        return;

    // 列表循环
    playIndex((m_index + 1) % m_playlist.size());
}

// 上一首
void PlaybackController::previous()
{
    if (m_playlist.empty() || !m_running)
        return;

    const size_t nextIndex = (m_index + m_playlist.size() - 1)
                           % m_playlist.size();
    playIndex(nextIndex);
}

// 暂停
void PlaybackController::togglePause()
{
    if (!m_player)
        return;

    m_paused = !m_paused;
    m_player->pause(m_paused);

    logging::INFO_LOG(
        "PlaybackController::togglePause", m_paused ? "paused" : "resumed");
}

// 音量调节
void PlaybackController::adjustVolume(float delta)
{
    m_volume = std::clamp(m_volume + delta, 0.0f, 1.0f);

    if (m_player)
        m_player->setVolume(m_volume);

    const std::string message =
        "volume: " + std::to_string(static_cast<int>(m_volume * 100)) + "%";
    logging::INFO_LOG("PlaybackController::adjustVolume", message.c_str());
}

// 相对位置seek
void PlaybackController::seekRelative(int64_t deltaUs)
{
    if (!m_player)
        return;

    int64_t targetUs = m_player->masterClock().positionUs() + deltaUs;
    const int64_t durationUs = m_player->durationUs();

    if (durationUs > 0)
        targetUs = std::clamp<int64_t>(targetUs, 0, durationUs);
    else if (targetUs < 0)
        targetUs = 0;

    if (m_presenter) {
        m_presenter->cancel();
        m_presenter.reset();
    }

    // 会话id标识跨文件的互斥
    const uint64_t sessionId = m_nextSessionId++;
    m_sessionId = sessionId;
    m_player->setSessionId(sessionId);
    installPlayerCallback(*m_player, sessionId);

    try {
        if (!m_player->seekTo(targetUs))
            throw std::runtime_error("media player is not started");

        if (m_player->videoEnabled())
            m_presenter = std::make_unique<VideoPresenter>(*m_player, sessionId);

        if (m_paused)
            m_player->pause(true);

        const std::string message =
            "seek: " + std::to_string(targetUs / 1'000'000) + "s";
        logging::INFO_LOG("PlaybackController::seekRelative", message.c_str());
    } catch (const std::exception& e) {
        reportError(std::string("seek failed: ") + e.what());
        stopCurrent();
        onTrackFailed();
    }
}

void PlaybackController::handleKey(SDL_Keycode key, bool repeat)
{
    // 除音量外 忽略按键自动重复 避免连续触发暂停/切歌/seek
    switch (key) {
    case SDLK_SPACE:
        if (!repeat) togglePause();
        break;
    case SDLK_N:
        if (!repeat) next();
        break;
    case SDLK_P:
        if (!repeat) previous();
        break;
    case SDLK_UP:
    case SDLK_PLUS:
    case SDLK_EQUALS:
        adjustVolume(kVolumeStep);
        break;
    case SDLK_DOWN:
    case SDLK_MINUS:
        adjustVolume(-kVolumeStep);
        break;
    case SDLK_RIGHT:
        if (!repeat) seekRelative(kSeekStepUs);
        break;
    case SDLK_LEFT:
        if (!repeat) seekRelative(-kSeekStepUs);
        break;
    case SDLK_Q:
    case SDLK_ESCAPE:
        m_running = false;
        break;
    default:
        break;
    }
}

void PlaybackController::handleAppEvent(const SDL_Event& event)
{
    const AppEventCode code = AppEvent::code(event);

    if (code == AppEventCode::Quit) {
        m_running = false;
        return;
    }

    if (code == AppEventCode::TerminalCommand) {
        handleTerminal(
            static_cast<TerminalCommand>(AppEvent::payload(event)));
        return;
    }

    if (AppEvent::sessionId(event) != m_sessionId)
        return;

    switch (code) {
    case AppEventCode::ClockStarted:
        if (m_presenter)
            m_presenter->onClockStarted();
        break;

    case AppEventCode::VideoFrameReady:
        if (m_presenter)
            m_presenter->onFrameReady();
        break;

    case AppEventCode::FrameDue:
        if (m_presenter)
            m_presenter->onFrameDue();
        break;

    case AppEventCode::PlaybackFinished:
        next();
        break;

    case AppEventCode::PlayerError:
        handlePlayerError();
        break;

    default:
        break;
    }
}

void PlaybackController::handleTerminal(TerminalCommand command)
{
    switch (command) {
    case TerminalCommand::Pause:
        togglePause();
        break;
    case TerminalCommand::Next:
        next();
        break;
    case TerminalCommand::Previous:
        previous();
        break;
    case TerminalCommand::VolumeUp:
        adjustVolume(kVolumeStep);
        break;
    case TerminalCommand::VolumeDown:
        adjustVolume(-kVolumeStep);
        break;
    case TerminalCommand::SeekForward:
        seekRelative(kSeekStepUs);
        break;
    case TerminalCommand::SeekBackward:
        seekRelative(-kSeekStepUs);
        break;
    case TerminalCommand::Quit:
        m_running = false;
        break;
    }
}

void PlaybackController::handlePlayerError()
{
    std::string message = "playback error";
    if (m_player)
        message = exceptionMessage(m_player->error());

    reportError(message);

    stopCurrent();
    onTrackFailed();
}

void PlaybackController::installPlayerCallback(MediaPlayer& player, uint64_t sessionId)
{
    player.setEventCallback([this, sessionId](PlayerEvent event) {
        (void)this;
        AppEventCode code = AppEventCode::PlayerError;

        switch (event) {
        case PlayerEvent::ClockStarted:
            code = AppEventCode::ClockStarted;
            break;
        case PlayerEvent::PlaybackFinished:
            code = AppEventCode::PlaybackFinished;
            break;
        case PlayerEvent::Error:
            code = AppEventCode::PlayerError;
            break;
        }

        AppEvent::push(code, sessionId);
    });
}

void PlaybackController::onTrackFailed()
{
    if (m_playlist.empty()) {
        m_running = false;
        m_exitCode = -1;
        return;
    }

    ++m_consecutiveFailures;

    if (m_consecutiveFailures >= m_playlist.size()) {
        m_running = false;
        m_exitCode = -1;
        return;
    }

    const size_t nextIndex = (m_index + 1) % m_playlist.size();
    playIndex(nextIndex);
}

void PlaybackController::reportError(const std::string& message)
{
    logging::ERROR_LOG("PlaybackController", message.c_str());
}

} // namespace avplayer
