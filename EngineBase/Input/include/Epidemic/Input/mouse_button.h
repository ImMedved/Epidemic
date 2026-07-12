#pragma once

#include <cstdint>
#include <string_view>

namespace epidemic::input
{
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
} 
