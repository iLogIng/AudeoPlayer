#pragma once

#include <SDL3/SDL_events.h>
#include <cstdint>

namespace avplayer
{

// 应用统一事件码 通过一个自定义 SDL 事件类型传递
enum class AppEventCode : int
{
    Quit,              // 外部退出信号
    PlayerError,       // 播放器内部错误
    ClockStarted,      // 主时钟开始运行
    VideoFrameReady,   // 视频解码线程已放入新帧
    FrameDue,          // 下一视频帧到达展示时间
    PlaybackFinished,  // 音频/视频均播放完成
    TerminalCommand,   // 终端输入命令
};

// 自定义 SDL 事件封装
// 事件仍交由SDL处理
class AppEvent
{
public:
    // 必须在 SDL_Init 之后调用
    static bool init();
    // 线程安全
    // sessionId 用于过滤旧播放会话/旧 seek 会话的迟到事件
    static bool push(AppEventCode code, uint64_t sessionId = 0,
                     intptr_t payload = 0);

    static bool isAppEvent(const SDL_Event& event);
    static AppEventCode code(const SDL_Event& event);
    static uint64_t sessionId(const SDL_Event& event);
    static intptr_t payload(const SDL_Event& event);
    static Uint32 type();

private:
    // SDL事件类型
    static Uint32 s_type;

}; // class AppEvent

} // namespace avplayer
