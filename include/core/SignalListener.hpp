#pragma once

#include <atomic>
#include <csignal>
#include <initializer_list>
#include <thread>

namespace avplayer
{

// 基于 signalfd 的信号监听器
class SignalListener
{
public:
    explicit SignalListener(std::initializer_list<int> signals = {SIGINT, SIGTERM, SIGHUP});
    ~SignalListener();

    SignalListener(const SignalListener&)            = delete;
    SignalListener& operator=(const SignalListener&) = delete;

    // 必须在 SDL_Init 和任何线程创建之前调用
    static bool blockSignals(std::initializer_list<int> signals = {SIGINT, SIGTERM, SIGHUP});

    bool start();
    bool stop();

    int lastSignal() const noexcept { return m_lastSignal.load(); }

private:
    void run();

    sigset_t m_mask{};
    int m_sigfd  = -1;
    int m_wakefd = -1;
    std::jthread m_thread;
    std::atomic<int> m_lastSignal{0};

}; // class SignalListener

} // namespace avplayer
