#pragma once

#include <SDL3/SDL_log.h>

namespace avplayer
{
namespace logging
{

// [STATUS|module]: message
inline constexpr const char *log_format = "[%s|%s]: %s";

enum class Category : int
{
    App   = SDL_LOG_CATEGORY_APPLICATION,
    Audio = SDL_LOG_CATEGORY_AUDIO,
    Video = SDL_LOG_CATEGORY_VIDEO,
};

inline void FAILED_LOG(const char *component, const char *msg) {
    SDL_LogError(static_cast<int>(Category::App), log_format, "FAILED", component, msg);
}
inline void ERROR_LOG(const char *component, const char *msg) {
    SDL_LogError(static_cast<int>(Category::App), log_format, "ERROR", component, msg);
}
inline void INFO_LOG(const char *component, const char *msg) {
    SDL_LogInfo(static_cast<int>(Category::App), log_format, "INFO", component, msg);
}

} // namespace logging

} // namespace avplayer
