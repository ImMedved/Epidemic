#pragma once

#include <cstdint>
#include <string_view>

namespace epidemic::input
{
// This file defines the normalized mouse buttons recognized by EngineBase input code.
// The enum includes Unknown so platform backends can reject unsupported raw button values explicitly.

enum class MouseButton : std::uint8_t
{
    Left = 0,
    Right = 1,
    Middle = 2,
    X1 = 3,
    X2 = 4,
    Count = 5,
    Unknown = 255,
};

// Returns true only for concrete supported buttons.
[[nodiscard]] constexpr bool IsKnownMouseButton(MouseButton button) noexcept
{
    switch (button)
    {
    case MouseButton::Left:
    case MouseButton::Right:
    case MouseButton::Middle:
    case MouseButton::X1:
    case MouseButton::X2:
        return true;
    case MouseButton::Count:
    case MouseButton::Unknown:
        return false;
    }

    return false;
}

// Converts a normalized mouse button to a stable diagnostic name.
[[nodiscard]] inline std::string_view ToString(MouseButton button) noexcept
{
    switch (button)
    {
    case MouseButton::Left:
        return "Left";
    case MouseButton::Right:
        return "Right";
    case MouseButton::Middle:
        return "Middle";
    case MouseButton::X1:
        return "X1";
    case MouseButton::X2:
        return "X2";
    case MouseButton::Count:
        return "Count";
    case MouseButton::Unknown:
        return "Unknown";
    }

    return "Unknown";
}
} // namespace epidemic::input