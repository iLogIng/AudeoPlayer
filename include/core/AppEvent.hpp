#pragma once

#include <SDL3/SDL_events.h>

namespace avplayer
{

// 应用统一事件码 通过一个自定义 SDL 事件类型传递
enum class AppEventCode : int
{
    Quit,          // 外部退出信号
    AudioDecodeEof, // 解码结束（不是播放结束）
    PlayerError,   // 播放器内部错误
    DrainTimeout,  // 设备缓冲排空完成
};

// 自定义 SDL 事件封装
class AppEvent
{
public:
    // 必须在 SDL_Init 之后调用
    static bool init();
    // 线程安全
    static bool push(AppEventCode code);

    static bool isAppEvent(const SDL_Event& event);
    static AppEventCode code(const SDL_Event& event);
    static Uint32 type();

private:
    // SDL事件类型
    static Uint32 s_type;

}; // class AppEvent

} // namespace avplayer
