#pragma once


// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.
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
} 
