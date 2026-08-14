#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

namespace epidemic::gameplay
{
struct GameplayDuration
{
    std::int64_t ticks = 0;

    [[nodiscard]] constexpr bool IsZero() const noexcept { return ticks == 0; }
    [[nodiscard]] constexpr bool operator==(const GameplayDuration&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const GameplayDuration&) const noexcept = default;
};

struct GameplayTimePoint
{
    std::int64_t ticks = 0;

    [[nodiscard]] constexpr bool operator==(const GameplayTimePoint&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const GameplayTimePoint&) const noexcept = default;
};

[[nodiscard]] constexpr std::optional<GameplayTimePoint> CheckedAdd(GameplayTimePoint point, GameplayDuration duration) noexcept
{
    if (duration.ticks > 0 && point.ticks > std::numeric_limits<std::int64_t>::max() - duration.ticks)
    {
        return std::nullopt;
    }
    if (duration.ticks < 0 && point.ticks < std::numeric_limits<std::int64_t>::min() - duration.ticks)
    {
        return std::nullopt;
    }
    return GameplayTimePoint{point.ticks + duration.ticks};
}

[[nodiscard]] constexpr std::optional<GameplayTimePoint> CheckedSubtract(GameplayTimePoint point, GameplayDuration duration) noexcept
{
    if (duration.ticks > 0 && point.ticks < std::numeric_limits<std::int64_t>::min() + duration.ticks)
    {
        return std::nullopt;
    }
    if (duration.ticks < 0 && point.ticks > std::numeric_limits<std::int64_t>::max() + duration.ticks)
    {
        return std::nullopt;
    }
    return GameplayTimePoint{point.ticks - duration.ticks};
}

[[nodiscard]] constexpr std::optional<GameplayDuration> CheckedDifference(GameplayTimePoint end, GameplayTimePoint begin) noexcept
{
    if (begin.ticks < 0 && end.ticks > std::numeric_limits<std::int64_t>::max() + begin.ticks)
    {
        return std::nullopt;
    }
    if (begin.ticks > 0 && end.ticks < std::numeric_limits<std::int64_t>::min() + begin.ticks)
    {
        return std::nullopt;
    }
    return GameplayDuration{end.ticks - begin.ticks};
}

[[nodiscard]] constexpr GameplayTimePoint SaturatingAdd(GameplayTimePoint point, GameplayDuration duration) noexcept
{
    if (const auto checked = CheckedAdd(point, duration))
    {
        return *checked;
    }
    return duration.ticks >= 0 ? GameplayTimePoint{std::numeric_limits<std::int64_t>::max()}
                               : GameplayTimePoint{std::numeric_limits<std::int64_t>::min()};
}

[[nodiscard]] constexpr GameplayTimePoint SaturatingSubtract(GameplayTimePoint point, GameplayDuration duration) noexcept
{
    if (const auto checked = CheckedSubtract(point, duration))
    {
        return *checked;
    }
    return duration.ticks >= 0 ? GameplayTimePoint{std::numeric_limits<std::int64_t>::min()}
                               : GameplayTimePoint{std::numeric_limits<std::int64_t>::max()};
}

[[nodiscard]] constexpr GameplayDuration SaturatingDifference(GameplayTimePoint end, GameplayTimePoint begin) noexcept
{
    if (const auto checked = CheckedDifference(end, begin))
    {
        return *checked;
    }
    return end.ticks >= begin.ticks ? GameplayDuration{std::numeric_limits<std::int64_t>::max()}
                                    : GameplayDuration{std::numeric_limits<std::int64_t>::min()};
}

[[nodiscard]] constexpr GameplayTimePoint operator+(GameplayTimePoint point, GameplayDuration duration) noexcept
{
    return SaturatingAdd(point, duration);
}

[[nodiscard]] constexpr GameplayTimePoint operator-(GameplayTimePoint point, GameplayDuration duration) noexcept
{
    return SaturatingSubtract(point, duration);
}

[[nodiscard]] constexpr GameplayDuration operator-(GameplayTimePoint end, GameplayTimePoint begin) noexcept
{
    return SaturatingDifference(end, begin);
}

struct GameplayTickId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const GameplayTickId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const GameplayTickId&) const noexcept = default;
};

struct Revision
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value; }
    [[nodiscard]] constexpr bool operator==(const Revision&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const Revision&) const noexcept = default;
};

struct GameplayTickContext
{
    GameplayTickId tick{};
    GameplayTimePoint time{};
    GameplayDuration delta{};
};
} // namespace epidemic::gameplay

namespace std
{
template <> struct hash<epidemic::gameplay::GameplayTickId>
{
    [[nodiscard]] size_t operator()(epidemic::gameplay::GameplayTickId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};
} // namespace std