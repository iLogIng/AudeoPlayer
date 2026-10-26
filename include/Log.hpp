#pragma once

#include <SDL3/SDL_log.h>

namespace avplayer
{
namespace
{

// [STATU|module]: message
static const char *log_format = "[%s|%s]: %s\n";
    
} // namespace

/*
SDL_LogCritical
SDL_LogDebug
SDL_LogError
SDL_LogInfo
SDL_LogMessage
SDL_LogMessageV
SDL_LogTrace
SDL_LogVerbose
SDL_LogWarn
*/

// TRACE < VERBOSE < DEBUG < INFO < WARN < ERROR < CRITICAL

namespace logging
{

enum class Category : int
{
    Audio = SDL_LOG_CATEGORY_AUDIO,
    Video = SDL_LOG_CATEGORY_VIDEO,
};

static inline void FAILED_LOG(const char *component, const char *msg) {
    SDL_LogInfo(static_cast<int>(Category::Audio), log_format, "FAILED", component, msg);
}
static inline void ERROR_LOG(const char *compoent, const char *msg) {
    SDL_LogError(static_cast<int>(Category::Audio), log_format, "ERROR", compoent, msg);
}
static inline void INFO_LOG(const char *compoent, const char *msg) {
    SDL_LogInfo(static_cast<int>(Category::Audio), log_format, "INFO", compoent, msg);
}

} // namespace logging


} // namespace avplayer

