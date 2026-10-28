#pragma once

#include <cstdint>

namespace avplayer
{

// 主时钟抽象
// 音视频同步以主时钟为基准 从时钟负责等待/丢帧
class MasterClock
{
public:
    virtual ~MasterClock() = default;

    virtual int64_t positionUs() const = 0;
    virtual bool paused() const = 0;
    virtual bool valid() const = 0;

}; // class MasterClock

} // namespace avplayer
