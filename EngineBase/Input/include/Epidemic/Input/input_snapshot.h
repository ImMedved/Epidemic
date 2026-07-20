#pragma once

#include <Epidemic/Input/keyboard_state.h>
#include <Epidemic/Input/mouse_state.h>

#include <cstdint>

namespace epidemic::input
{
// This file defines the frame-stable input snapshot published by InputSystem.
// Consumers read this snapshot after PublishSnapshot() to observe normalized keyboard and mouse state.

struct InputSnapshot
{
    std::uint64_t update_index{};
    KeyboardState keyboard;
    MouseState mouse;

    // Returns whether the owning window had focus when this snapshot was published.
    [[nodiscard]] bool HasFocus() const noexcept
    {
        return mouse.HasFocus();
    }

    // Returns whether the owning window had mouse capture when this snapshot was published.
    [[nodiscard]] bool HasCapture() const noexcept
    {
        return mouse.HasCapture();
    }
};
} 
