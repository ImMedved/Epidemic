#pragma once

#include "Epidemic/Runtime/Foundation/runtime_time.h"

namespace epidemic::runtime
{
using GameTime = GameTimePoint;

[[nodiscard]] constexpr bool IsZero(GameDuration duration) noexcept
{
    return duration.ticks == 0;
}
} // namespace epidemic::runtime
