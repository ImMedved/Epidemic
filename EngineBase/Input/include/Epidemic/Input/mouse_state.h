#pragma once

#include <Epidemic/Input/mouse_button.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace epidemic::input
{
class MouseState
{
  public:
    static constexpr std::size_t ButtonCount = static_cast<std::size_t>(MouseButton::Count);

    [[nodiscard]] std::int32_t PositionX() const noexcept
    {
        return position_x_;
    }

    [[nodiscard]] std::int32_t PositionY() const noexcept
    {
        return position_y_;
    }

    [[nodiscard]] std::int32_t DeltaX() const noexcept
    {
        return delta_x_;
    }

    [[nodiscard]] std::int32_t DeltaY() const noexcept
    {
        return delta_y_;
    }

    [[nodiscard]] std::int32_t WheelDelta() const noexcept
    {
        return wheel_delta_;
    }

    [[nodiscard]] bool IsButtonDown(MouseButton button) const noexcept
    {
        return buttons_down_[ToIndex(button)];
    }

    [[nodiscard]] bool WasPressedThisFrame(MouseButton button) const noexcept
    {
        return buttons_pressed_[ToIndex(button)];
    }

    [[nodiscard]] bool WasReleasedThisFrame(MouseButton button) const noexcept
    {
        return buttons_released_[ToIndex(button)];
    }

    [[nodiscard]] bool HasFocus() const noexcept
    {
        return has_focus_;
    }

    [[nodiscard]] bool HasCapture() const noexcept
    {
        return has_capture_;
    }

    void SetPosition(std::int32_t x, std::int32_t y) noexcept
    {
        delta_x_ += x - position_x_;
        delta_y_ += y - position_y_;
        position_x_ = x;
        position_y_ = y;
    }

    void AddWheelDelta(std::int32_t wheel_delta) noexcept
    {
        wheel_delta_ += wheel_delta;
    }

    void SetButtonDown(MouseButton button, bool down) noexcept
    {
        buttons_down_[ToIndex(button)] = down;
    }

    void MarkPressed(MouseButton button) noexcept
    {
        buttons_pressed_[ToIndex(button)] = true;
    }

    void MarkReleased(MouseButton button) noexcept
    {
        buttons_released_[ToIndex(button)] = true;
    }

    void SetFocus(bool focused) noexcept
    {
        has_focus_ = focused;
    }

    void SetCapture(bool captured) noexcept
    {
        has_capture_ = captured;
    }

    void ClearButtons() noexcept
    {
        buttons_down_.fill(false);
        buttons_pressed_.fill(false);
        buttons_released_.fill(false);
    }

    void ClearTransient() noexcept
    {
        delta_x_ = 0;
        delta_y_ = 0;
        wheel_delta_ = 0;
        buttons_pressed_.fill(false);
        buttons_released_.fill(false);
    }

  private:
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
