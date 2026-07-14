#pragma once

#include <compare>
#include <cstdint>

namespace epidemic::runtime
{
struct GameDuration
{
    std::int64_t ticks = 0;

    [[nodiscard]] constexpr bool operator==(const GameDuration&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const GameDuration&) const noexcept = default;
};

struct GameTimePoint
{
    std::int64_t ticks = 0;

    [[nodiscard]] constexpr bool operator==(const GameTimePoint&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const GameTimePoint&) const noexcept = default;
};

[[nodiscard]] constexpr GameTimePoint operator+(GameTimePoint point, GameDuration duration) noexcept
{
    return GameTimePoint{point.ticks + duration.ticks};
}

[[nodiscard]] constexpr GameDuration operator-(GameTimePoint end, GameTimePoint begin) noexcept
{
    return GameDuration{end.ticks - begin.ticks};
}
} // namespace epidemic::runtime

