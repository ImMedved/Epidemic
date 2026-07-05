#pragma once

#include <Epidemic/Input/key_code.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace epidemic::input
{
class KeyboardState
{
  public:
    static constexpr std::size_t KeyCount = 256;

    [[nodiscard]] bool IsKeyDown(KeyCode key_code) const noexcept
    {
        return keys_down_[ToIndex(key_code)];
    }

    [[nodiscard]] bool WasPressedThisFrame(KeyCode key_code) const noexcept
    {
        return keys_pressed_[ToIndex(key_code)];
    }

    [[nodiscard]] bool WasReleasedThisFrame(KeyCode key_code) const noexcept
    {
        return keys_released_[ToIndex(key_code)];
    }

    void SetKeyDown(KeyCode key_code, bool down) noexcept
    {
        keys_down_[ToIndex(key_code)] = down;
    }

    void MarkPressed(KeyCode key_code) noexcept
    {
        keys_pressed_[ToIndex(key_code)] = true;
    }

    void MarkReleased(KeyCode key_code) noexcept
    {
        keys_released_[ToIndex(key_code)] = true;
    }

    void ClearTransient() noexcept
    {
        keys_pressed_.fill(false);
        keys_released_.fill(false);
    }

    void ClearAll() noexcept
    {
        keys_down_.fill(false);
        ClearTransient();
    }

  private:
    [[nodiscard]] static constexpr std::size_t ToIndex(KeyCode key_code) noexcept
    {
        const auto index = static_cast<std::size_t>(static_cast<std::uint16_t>(key_code));
        return index < KeyCount ? index : 0;
    }

    std::array<bool, KeyCount> keys_down_{};
    std::array<bool, KeyCount> keys_pressed_{};
    std::array<bool, KeyCount> keys_released_{};
};
} // namespace epidemic::input
