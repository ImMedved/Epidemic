#pragma once

#include <Epidemic/Input/mouse_button.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace epidemic::input
{
// This file defines the mouse portion of the input snapshot.
// MouseState tracks absolute position, per-frame deltas, wheel accumulation, button transitions,
// focus state, and capture state.

class MouseState
{
  public:
    static constexpr std::size_t ButtonCount = static_cast<std::size_t>(MouseButton::Count);

    // Returns the last known cursor X position.
    [[nodiscard]] std::int32_t PositionX() const noexcept
    {
        return position_x_;
    }

    // Returns the last known cursor Y position.
    [[nodiscard]] std::int32_t PositionY() const noexcept
    {
        return position_y_;
    }

    // Returns the accumulated X delta for the current frame.
    [[nodiscard]] std::int32_t DeltaX() const noexcept
    {
        return delta_x_;
    }

    // Returns the accumulated Y delta for the current frame.
    [[nodiscard]] std::int32_t DeltaY() const noexcept
    {
        return delta_y_;
    }

    // Returns the accumulated wheel delta for the current frame.
    [[nodiscard]] std::int32_t WheelDelta() const noexcept
    {
        return wheel_delta_;
    }

    // Returns whether the specified button is currently held down.
    [[nodiscard]] bool IsButtonDown(MouseButton button) const noexcept
    {
        return buttons_down_[ToIndex(button)];
    }

    // Returns whether the specified button was pressed during the current frame.
    [[nodiscard]] bool WasPressedThisFrame(MouseButton button) const noexcept
    {
        return buttons_pressed_[ToIndex(button)];
    }

    // Returns whether the specified button was released during the current frame.
    [[nodiscard]] bool WasReleasedThisFrame(MouseButton button) const noexcept
    {
        return buttons_released_[ToIndex(button)];
    }

    // Returns whether the owning window currently has focus.
    [[nodiscard]] bool HasFocus() const noexcept
    {
        return has_focus_;
    }

    // Returns whether the owning window currently has mouse capture.
    [[nodiscard]] bool HasCapture() const noexcept
    {
        return has_capture_;
    }

    // Updates the cursor position and accumulates per-frame deltas.
    void SetPosition(std::int32_t x, std::int32_t y) noexcept
    {
        delta_x_ += x - position_x_;
        delta_y_ += y - position_y_;
        position_x_ = x;
        position_y_ = y;
    }

    // Adds one wheel delta contribution to the current frame.
    void AddWheelDelta(std::int32_t wheel_delta) noexcept
    {
        wheel_delta_ += wheel_delta;
    }

    // Sets the persistent down/up state for one mouse button.
    void SetButtonDown(MouseButton button, bool down) noexcept
    {
        buttons_down_[ToIndex(button)] = down;
    }

    // Marks a button as newly pressed for the current frame.
    void MarkPressed(MouseButton button) noexcept
    {
        buttons_pressed_[ToIndex(button)] = true;
    }

    // Marks a button as newly released for the current frame.
    void MarkReleased(MouseButton button) noexcept
    {
        buttons_released_[ToIndex(button)] = true;
    }

    // Updates cached focus state.
    void SetFocus(bool focused) noexcept
    {
        has_focus_ = focused;
    }

    // Updates cached capture state.
    void SetCapture(bool captured) noexcept
    {
        has_capture_ = captured;
    }

    // Clears all button down and transient flags.
    void ClearButtons() noexcept
    {
        buttons_down_.fill(false);
        buttons_pressed_.fill(false);
        buttons_released_.fill(false);
    }

    // Clears only per-frame movement, wheel, and transition data.
    void ClearTransient() noexcept
    {
        delta_x_ = 0;
        delta_y_ = 0;
        wheel_delta_ = 0;
        buttons_pressed_.fill(false);
        buttons_released_.fill(false);
    }

  private:
    // Maps a mouse button to a safe array index, collapsing invalid values to index zero.
    [[nodiscard]] static constexpr std::size_t ToIndex(MouseButton button) noexcept
    {
        const auto index = static_cast<std::size_t>(button);
        return index < ButtonCount ? index : 0;
    }

    std::int32_t position_x_{};
    std::int32_t position_y_{};
    std::int32_t delta_x_{};
    std::int32_t delta_y_{};
    std::int32_t wheel_delta_{};
    std::array<bool, ButtonCount> buttons_down_{};
    std::array<bool, ButtonCount> buttons_pressed_{};
    std::array<bool, ButtonCount> buttons_released_{};
    bool has_focus_{false};
    bool has_capture_{false};
};
} 
