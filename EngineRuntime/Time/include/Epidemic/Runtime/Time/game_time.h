#pragma once

#include <cstdint>

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

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
};

struct GameTime
{
    std::int64_t ticks = 0;

    [[nodiscard]] constexpr bool operator==(const GameTime&) const noexcept = default;
};

[[nodiscard]] constexpr GameTime operator+(GameTime time, GameDuration duration) noexcept
{
    return GameTime{time.ticks + duration.ticks};
}

[[nodiscard]] constexpr GameDuration operator-(GameTime left, GameTime right) noexcept
{
    return GameDuration{left.ticks - right.ticks};
}
} // namespace epidemic::runtime
