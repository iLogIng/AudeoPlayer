#include "core/TerminalInput.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "core/AppEvent.hpp"
#include "Log.hpp"

namespace avplayer
{

namespace
{

bool mapTerminalKey(char key, TerminalCommand& command)
{
    switch (key) {
    case ' ':
        command = TerminalCommand::Pause;
        return true;
    case 'n':
    case 'N':
        command = TerminalCommand::Next;
        return true;
    case 'p':
    case 'P':
        command = TerminalCommand::Previous;
        return true;
    case '+':
    case '=':
        command = TerminalCommand::VolumeUp;
        return true;
    case '-':
        command = TerminalCommand::VolumeDown;
        return true;
    case '.':
    case '>':
        command = TerminalCommand::SeekForward;
        return true;
    case ',':
    case '<':
        command = TerminalCommand::SeekBackward;
        return true;
    case 'q':
    case 'Q':
        command = TerminalCommand::Quit;
        return true;
    default:
        return false;
    }
}

} // namespace

TerminalInput::~TerminalInput()
{
    stop();

    if (m_wakefd >= 0) {
        ::close(m_wakefd);
        m_wakefd = -1;
    }
}

bool TerminalInput::start()
{
    if (m_thread.joinable())
        return true;

    if (!::isatty(STDIN_FILENO))
        return false;

    if (::tcgetattr(STDIN_FILENO, &m_old) != 0) {
        logging::FAILED_LOG("TerminalInput::start", std::strerror(errno));
        return false;
    }

    termios raw = m_old;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        logging::FAILED_LOG("TerminalInput::start", std::strerror(errno));
        return false;
    }
    m_raw = true;

    if (m_wakefd < 0) {
        m_wakefd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (m_wakefd < 0) {
            logging::FAILED_LOG("TerminalInput::start", std::strerror(errno));
            restore();
            return false;
        }
    }

    m_thread = std::jthread([this] { run(); });
    return true;
}

void TerminalInput::stop()
{
    if (m_thread.joinable()) {
        if (m_wakefd >= 0) {
            const uint64_t one = 1;
            (void)::write(m_wakefd, &one, sizeof(one));
        }
        m_thread.join();
    }

    restore();
}

void TerminalInput::restore()
{
    if (!m_raw) return;
    ::tcsetattr(STDIN_FILENO, TCSANOW, &m_old);
    m_raw = false;
}

void TerminalInput::run()
{
    while (true) {
        pollfd fds[2] = {
            { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 },
            { .fd = m_wakefd,     .events = POLLIN, .revents = 0 },
        };

        const int rc = ::poll(fds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) continue;

        if (fds[1].revents & POLLIN) {
            uint64_t value = 0;
            (void)::read(m_wakefd, &value, sizeof(value));
            break;
        }

        // 终端关闭或不可用时退出输入线程
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
            break;

        if (fds[0].revents & POLLIN) {
            char buffer[32];
            const ssize_t n = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0) break;

            for (ssize_t i = 0; i < n; ++i) {
                TerminalCommand command;
                if (!mapTerminalKey(buffer[i], command))
                    continue;

                AppEvent::push(
                    AppEventCode::TerminalCommand, 0,
                    static_cast<intptr_t>(command));
            }
        }
    }
}

} // namespace avplayer
