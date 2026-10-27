#include "core/SignalListener.hpp"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <pthread.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "core/AppEvent.hpp"
#include "Log.hpp"

namespace avplayer
{

SignalListener::SignalListener(std::initializer_list<int> signals)
{
    sigemptyset(&m_mask);
    for (int s : signals) {
        sigaddset(&m_mask, s);
    }

    // 遮盖系统信号
    if (::pthread_sigmask(SIG_BLOCK, &m_mask, nullptr) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SignalListener: pthread_sigmask failed: %s", std::strerror(errno));
        throw std::runtime_error("SignalListener: pthread_sigmask failed");
    }

    // 获得信号文件描述符
    m_sigfd = ::signalfd(-1, &m_mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (m_sigfd < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SignalListener: signalfd failed: %s", std::strerror(errno));
        throw std::runtime_error("SignalListener: signalfd failed");
    }

    // eventfd 用于正常退出时唤醒 poll
    m_wakefd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (m_wakefd < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SignalListener: eventfd failed: %s", std::strerror(errno));
        ::close(m_sigfd);
        m_sigfd = -1;
        throw std::runtime_error("SignalListener: eventfd failed");
    }
}

SignalListener::~SignalListener()
{
    stop();

    if (m_sigfd >= 0) {
        ::close(m_sigfd);
        m_sigfd = -1;
    }
    if (m_wakefd >= 0) {
        ::close(m_wakefd);
        m_wakefd = -1;
    }
}

// 阻拦信号
bool SignalListener::blockSignals(std::initializer_list<int> signals)
{
    sigset_t mask;
    sigemptyset(&mask);
    for (int s : signals) {
        sigaddset(&mask, s);
    }

    // 线程信号遮蔽
    if (::pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SignalListener: pthread_sigmask failed: %s", std::strerror(errno));
        return false;
    }
    return true;
}

// 启动
bool SignalListener::start()
{
    if (m_sigfd < 0 || m_wakefd < 0) return false;
    if (m_thread.joinable()) return true;

    m_thread = std::jthread([this] { run(); });
    return true;
}

// 停止
bool SignalListener::stop()
{
    if (!m_thread.joinable()) return true;

    if (m_wakefd >= 0) {
        const uint64_t one = 1;
        // EAGAIN 表示唤醒信号已经存在
        (void)::write(m_wakefd, &one, sizeof(one));
    }

    m_thread.join();
    return true;
}

void SignalListener::run()
{
    while (true) {
        // 轮询描述符 信号描述符 唤醒描述符
        pollfd fds[2] = {
            { .fd = m_sigfd,  .events = POLLIN, .revents = 0 },
            { .fd = m_wakefd, .events = POLLIN, .revents = 0 },
        };

        // 无超时阻塞等待 信号或退出唤醒
        const int rc = ::poll(fds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) continue;

        // 优先处理正常退出
        if (fds[1].revents & POLLIN) {
            uint64_t value = 0;
            ::read(m_wakefd, &value, sizeof(value));
            break;
        }

        if (fds[0].revents & POLLIN) {
            signalfd_siginfo info{};
            const ssize_t n = ::read(m_sigfd, &info, sizeof(info));
            if (n == static_cast<ssize_t>(sizeof(info))) {
                m_lastSignal.store(static_cast<int>(info.ssi_signo));
                // 退出 事件码
                AppEvent::push(AppEventCode::Quit);
                break;
            }
        }
    }
}

} // namespace avplayer
