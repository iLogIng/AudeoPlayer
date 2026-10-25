#pragma once

#include <atomic>
#include <thread>

namespace avplayer
{

class Thread
{
protected:
    std::thread m_thread;
    std::atomic<bool> m_abort{false};
    virtual void onAbort() = 0;
    virtual void run() = 0;

public:
    void start() { m_thread = std::thread([this]{ run(); }); }
    void stop() {
        m_abort.store(true);
        onAbort();
        if (m_thread.joinable()) m_thread.join();
    }
    bool isAbort() { return m_abort; }

};

} // namespace avplayer

