#include "core/AppEvent.hpp"

#include "Log.hpp"

namespace avplayer
{

Uint32 AppEvent::s_type = static_cast<Uint32>(-1);

// SDL_Init后初始化
bool AppEvent::init()
{
    if (s_type != static_cast<Uint32>(-1)) return true;

    // 注册SDL事件
    const Uint32 eventType = SDL_RegisterEvents(1);
    if (eventType == static_cast<Uint32>(-1)) {
        logging::FAILED_LOG("AppEvent::init", "can not register SDL events");
        return false;
    }

    s_type = eventType;
    return true;
}

// 压入SDL事件队列
bool AppEvent::push(AppEventCode code)
{
    if (s_type == static_cast<Uint32>(-1)) return false;

    SDL_Event event{};
    // 获取事件类型
    event.type = s_type;
    event.user.code = static_cast<int>(code);
    return SDL_PushEvent(&event);
}

bool AppEvent::isAppEvent(const SDL_Event& event)
{
    return event.type == s_type;
}

AppEventCode AppEvent::code(const SDL_Event& event)
{
    return static_cast<AppEventCode>(event.user.code);
}

// 事件类型
Uint32 AppEvent::type()
{
    return s_type;
}

} // namespace avplayer
