#pragma once

#include <cstdint>
#include <limits>

namespace epidemic::core::detail
{
// Frame indices are monotonic and saturate instead of wrapping to the initial frame identity.
[[nodiscard]] constexpr std::uint64_t AdvanceExecutedFrameCount(std::uint64_t current) noexcept
{
    return current == std::numeric_limits<std::uint64_t>::max() ? current : current + 1;
}
} // namespace epidemic::core::detail
