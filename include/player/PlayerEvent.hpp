#pragma once

namespace avplayer
{

enum class PlayerEvent
{
    AudioDecodeEof,
    VideoDecodeEof,
    Error,
};

} // namespace avplayer
