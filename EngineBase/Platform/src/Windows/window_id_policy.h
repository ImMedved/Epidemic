#pragma once

#include <Epidemic/Platform/platform_event.h>

#include <limits>

namespace epidemic::platform::detail
{
// Reserves zero as invalid and max as the exhausted sentinel so the generator never wraps.
[[nodiscard]] constexpr bool CanAllocateWindowId(WindowId next_id) noexcept
{
    return next_id != kInvalidWindowId && next_id != std::numeric_limits<WindowId>::max();
}

// Advances only an allocatable id. The last allocatable value advances to the exhausted max sentinel.
[[nodiscard]] constexpr WindowId AdvanceWindowId(WindowId current_id) noexcept
{
    return current_id + 1;
}
} // namespace epidemic::platform::detail
