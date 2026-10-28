#pragma once

#include <termios.h>
#include <thread>

namespace avplayer
{

// 终端交互命令
enum class TerminalCommand : int
{
    Pause,
    Next,
    Previous,
    VolumeUp,
    VolumeDown,
    SeekForward,
    SeekBackward,
    Quit,
};

// 读取终端按键并转换为 AppEvent
// 仅在 stdin 是 TTY 时启用 raw mode
class TerminalInput
{
public:
    TerminalInput() = default;
    ~TerminalInput();

    TerminalInput(const TerminalInput&)            = delete;
    TerminalInput& operator=(const TerminalInput&) = delete;

    bool start();
    void stop();

private:
    void run();
    void restore();

    bool m_raw = false;
    int m_wakefd = -1;
    termios m_old{};
    std::jthread m_thread;

}; // class TerminalInput

} // namespace avplayer
