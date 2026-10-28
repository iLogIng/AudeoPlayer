#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <cstdint>
#include <cstdio>

#include "core/AppEvent.hpp"

namespace
{

int g_failures = 0;

void check(bool condition, const char *message)
{
    if (condition) return;

    std::fprintf(stderr, "[FAIL] %s\n", message);
    ++g_failures;
}

void checkEqual(uint64_t actual, uint64_t expected, const char *message)
{
    if (actual == expected) return;

    std::fprintf(stderr, "[FAIL] %s: actual=%llu expected=%llu\n",
                 message, static_cast<unsigned long long>(actual),
                 static_cast<unsigned long long>(expected));
    ++g_failures;
}

} // namespace

int main()
{
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    if (!avplayer::AppEvent::init()) {
        std::fprintf(stderr, "[FAIL] AppEvent::init\n");
        SDL_Quit();
        return 1;
    }

    check(avplayer::AppEvent::type() != static_cast<Uint32>(-1),
          "event type registered");

    // 带 sessionId 与 payload 的事件
    check(avplayer::AppEvent::push(
              avplayer::AppEventCode::PlaybackFinished, 0x1234, 42),
          "push event");

    SDL_Event event{};
    bool found = false;
    while (SDL_PollEvent(&event)) {
        if (!avplayer::AppEvent::isAppEvent(event))
            continue;

        found = true;
        check(avplayer::AppEvent::code(event)
                  == avplayer::AppEventCode::PlaybackFinished,
              "event code");
        checkEqual(avplayer::AppEvent::sessionId(event), 0x1234,
                   "event session id");
        checkEqual(static_cast<uint64_t>(avplayer::AppEvent::payload(event)),
                   42, "event payload");
    }
    check(found, "event received");

    // 默认 sessionId 与 payload 为 0
    check(avplayer::AppEvent::push(avplayer::AppEventCode::Quit),
          "push default event");

    found = false;
    while (SDL_PollEvent(&event)) {
        if (!avplayer::AppEvent::isAppEvent(event))
            continue;

        found = true;
        check(avplayer::AppEvent::code(event) == avplayer::AppEventCode::Quit,
              "default event code");
        checkEqual(avplayer::AppEvent::sessionId(event), 0,
                   "default session id");
        checkEqual(static_cast<uint64_t>(avplayer::AppEvent::payload(event)),
                   0, "default payload");
    }
    check(found, "default event received");

    SDL_Quit();

    if (g_failures > 0) {
        std::fprintf(stderr, "test_app_event: %d failure(s)\n", g_failures);
        return 1;
    }

    std::printf("test_app_event: OK\n");
    return 0;
}
