#pragma once

#include <cstdint>
#include <limits>

namespace epidemic::input::detail
{
// Advances the publication cursor monotonically and saturates at the final representable value.
[[nodiscard]] constexpr std::uint64_t AdvanceUpdateIndex(std::uint64_t current) noexcept
{
    return current == std::numeric_limits<std::uint64_t>::max() ? current : current + 1;
}

// Computes one published mouse delta without overflowing the public 32-bit representation.
[[nodiscard]] constexpr std::int32_t SaturatingDifference(std::int32_t current, std::int32_t previous) noexcept
{
    const auto difference = static_cast<std::int64_t>(current) - static_cast<std::int64_t>(previous);
    if (difference < std::numeric_limits<std::int32_t>::min())
    {
        return std::numeric_limits<std::int32_t>::min();
    }
    if (difference > std::numeric_limits<std::int32_t>::max())
    {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(difference);
}
} // namespace epidemic::input::detail
