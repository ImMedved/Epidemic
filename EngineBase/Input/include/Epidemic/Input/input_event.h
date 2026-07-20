#pragma once

#include <Epidemic/Input/key_code.h>
#include <Epidemic/Input/mouse_button.h>

#include <cstdint>

namespace epidemic::input
{
// This file defines the transient input events emitted during one PublishSnapshot() call.
// InputEvent complements InputSnapshot by preserving per-event details such as repeat and wheel deltas.

enum class InputEventType : std::uint8_t
{
    None,
    KeyPressed,
    KeyReleased,
    MouseMoved,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseWheel,
    FocusChanged,
    CaptureChanged,
};

struct InputEvent
{
    InputEventType type{InputEventType::None};
    KeyCode key_code{KeyCode::Unknown};
    MouseButton mouse_button{MouseButton::Left};
    bool repeated{};
    bool focused{};
    bool captured{};
    std::int32_t mouse_x{};
    std::int32_t mouse_y{};
    std::int32_t mouse_delta_x{};
    std::int32_t mouse_delta_y{};
    std::int32_t wheel_delta{};
};
} 
