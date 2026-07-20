#pragma once

namespace epidemic::runtime
{
enum class SeasonKind
{
    Spring,
    Summer,
    Autumn,
    Winter,
    Transitioning,
};

struct SeasonState
{
    SeasonKind kind = SeasonKind::Spring;
    float progress = 0.0f;

    [[nodiscard]] constexpr bool operator==(const SeasonState&) const noexcept = default;
};
} // namespace epidemic::runtime
