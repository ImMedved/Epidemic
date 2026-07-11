#pragma once

#include <cstdint>

namespace epidemic::platform
{
// This file defines the raw event payload emitted by the platform layer.
// PlatformEvent is intentionally a flat POD-style structure so the window runtime can queue events cheaply.

using WindowId = std::uint64_t;

constexpr WindowId kInvalidWindowId = 0;

enum class PlatformEventType : std::uint8_t
{
    None,
    WindowCloseRequested,
    WindowResized,
    WindowFocusChanged,
    WindowMinimized,
    WindowRestored,
    KeyPressed,
    KeyReleased,
    MouseMoved,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseWheel,
    MouseCaptureChanged,
};

// Carries one normalized platform event.
// Relationship: InputSystem consumes the input-related subset, while apps may inspect window events directly.
struct PlatformEvent
{
    PlatformEventType type{PlatformEventType::None};
    WindowId window_id{kInvalidWindowId};
    std::uint32_t client_width{};
    std::uint32_t client_height{};
    bool focused{};
    bool captured{};
    std::uint32_t key_code{};
    std::uint32_t scan_code{};
    bool repeated{};
    std::int32_t mouse_x{};
    std::int32_t mouse_y{};
    std::int32_t mouse_delta_x{};
    std::int32_t mouse_delta_y{};
    std::int32_t wheel_delta{};
    std::uint8_t mouse_button{};
};
} // namespace epidemic::platform