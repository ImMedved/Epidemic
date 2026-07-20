#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

namespace epidemic::runtime
{
struct RuntimeFrameDuration
{
    std::chrono::microseconds value{};

    [[nodiscard]] constexpr bool IsZero() const noexcept
    {
        return value.count() == 0;
    }

    [[nodiscard]] constexpr bool IsNegative() const noexcept
    {
        return value.count() < 0;
    }

    [[nodiscard]] constexpr bool operator==(const RuntimeFrameDuration&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const RuntimeFrameDuration&) const noexcept = default;
};

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
    if (duration.ticks > 0 && point.ticks < std::numeric_limits<std::int64_t>::min() + duration.ticks)
    {
        return std::nullopt;
    }
    if (duration.ticks < 0 && point.ticks > std::numeric_limits<std::int64_t>::max() + duration.ticks)
    {
        return std::nullopt;
    }
    return GameTimePoint{point.ticks - duration.ticks};
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

[[nodiscard]] constexpr GameTimePoint SaturatingAdd(GameTimePoint point, GameDuration duration) noexcept
{
    if (const auto checked = CheckedAdd(point, duration))
    {
        return *checked;
    }
    return duration.ticks >= 0 ? GameTimePoint{std::numeric_limits<std::int64_t>::max()}
                               : GameTimePoint{std::numeric_limits<std::int64_t>::min()};
}

[[nodiscard]] constexpr GameTimePoint SaturatingSubtract(GameTimePoint point, GameDuration duration) noexcept
{
    if (const auto checked = CheckedSubtract(point, duration))
    {
        return *checked;
    }
    return duration.ticks >= 0 ? GameTimePoint{std::numeric_limits<std::int64_t>::min()}
                               : GameTimePoint{std::numeric_limits<std::int64_t>::max()};
}

[[nodiscard]] constexpr GameDuration SaturatingDifference(GameTimePoint end, GameTimePoint begin) noexcept
{
    if (const auto checked = CheckedDifference(end, begin))
    {
        return *checked;
    }
    return end.ticks >= begin.ticks ? GameDuration{std::numeric_limits<std::int64_t>::max()}
                                    : GameDuration{std::numeric_limits<std::int64_t>::min()};
}

// Convenience operators use saturating semantics. Use CheckedAdd/CheckedSubtract/
// CheckedDifference when overflow must be reported instead of clamped.
[[nodiscard]] constexpr GameTimePoint operator+(GameTimePoint point, GameDuration duration) noexcept
{
    return SaturatingAdd(point, duration);
}

[[nodiscard]] constexpr GameTimePoint operator-(GameTimePoint point, GameDuration duration) noexcept
{
    return SaturatingSubtract(point, duration);
}

[[nodiscard]] constexpr GameDuration operator-(GameTimePoint end, GameTimePoint begin) noexcept
{
    return SaturatingDifference(end, begin);
}
} // namespace epidemic::runtime
