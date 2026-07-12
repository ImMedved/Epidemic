#pragma once

#include <Epidemic/Input/keyboard_state.h>
#include <Epidemic/Input/mouse_state.h>

#include <cstdint>

namespace epidemic::input
{
struct InputSnapshot
{
    std::uint64_t update_index{};
    KeyboardState keyboard;
    MouseState mouse;

    [[nodiscard]] bool HasFocus() const noexcept
    {
        return mouse.HasFocus();
    }

    [[nodiscard]] bool HasCapture() const noexcept
    {
        return mouse.HasCapture();
    }
};
} 
