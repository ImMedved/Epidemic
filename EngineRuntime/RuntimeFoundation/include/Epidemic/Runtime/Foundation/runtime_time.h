#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

namespace epidemic::runtime
{
struct GameDuration
{
    std::int64_t ticks = 0;

    [[nodiscard]] constexpr bool IsZero() const noexcept
    {
        return ticks == 0;
    }

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

[[nodiscard]] constexpr GameTimePoint operator-(GameTimePoint point, GameDuration duration) noexcept
{
    return GameTimePoint{point.ticks - duration.ticks};
}

[[nodiscard]] constexpr GameDuration operator-(GameTimePoint end, GameTimePoint begin) noexcept
{
    return GameDuration{end.ticks - begin.ticks};
}

[[nodiscard]] constexpr std::optional<GameTimePoint> CheckedAdd(GameTimePoint point, GameDuration duration) noexcept
{
    if (duration.ticks > 0 && point.ticks > std::numeric_limits<std::int64_t>::max() - duration.ticks)
    {
        return std::nullopt;
    }
    if (duration.ticks < 0 && point.ticks < std::numeric_limits<std::int64_t>::min() - duration.ticks)
    {
        return std::nullopt;
    }
    return GameTimePoint{point.ticks + duration.ticks};
}

[[nodiscard]] constexpr std::optional<GameTimePoint> CheckedSubtract(GameTimePoint point, GameDuration duration) noexcept
{
    if (duration.ticks == std::numeric_limits<std::int64_t>::min())
    {
        return std::nullopt;
    }
    return CheckedAdd(point, GameDuration{-duration.ticks});
}

[[nodiscard]] constexpr std::optional<GameDuration> CheckedDifference(GameTimePoint end, GameTimePoint begin) noexcept
{
    if (begin.ticks < 0 && end.ticks > std::numeric_limits<std::int64_t>::max() + begin.ticks)
    {
        return std::nullopt;
    }
    if (begin.ticks > 0 && end.ticks < std::numeric_limits<std::int64_t>::min() + begin.ticks)
    {
        return std::nullopt;
    }
    return GameDuration{end.ticks - begin.ticks};
}
} // namespace epidemic::runtime

