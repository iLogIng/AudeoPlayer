#pragma once

#include <SDL3/SDL_log.h>

namespace avplayer
{
namespace
{

// [STATU|module]: message
static const char *log_format = "[%s|%s]: %s\n";
    
} // namespace


namespace logging
{

static inline void FAILED_LOG(const char *compoent, const char *msg) {
    SDL_Log(log_format, "FAILED", compoent, msg);
}
static inline void ERROR_LOG(const char *compoent, const char *msg) {
    SDL_Log(log_format, "ERROR", compoent, msg);
}
static inline void INFO_LOG(const char *compoent, const char *msg) {
    SDL_Log(log_format, "INFO", compoent, msg);
}

} // namespace logging


} // namespace avplayer

