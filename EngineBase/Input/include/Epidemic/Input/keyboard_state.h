#pragma once

#include <Epidemic/Input/key_code.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace epidemic::input
{
// This file defines the keyboard portion of the input snapshot.
// KeyboardState tracks persistent key-down state plus transient pressed/released flags for the current frame.

class KeyboardState
{
  public:
    static constexpr std::size_t KeyCount = 256;

    // Returns whether the key is currently held down.
    [[nodiscard]] bool IsKeyDown(KeyCode key_code) const noexcept
    {
        return keys_down_[ToIndex(key_code)];
    }

    // Returns whether the key transitioned to pressed during the current frame.
    [[nodiscard]] bool WasPressedThisFrame(KeyCode key_code) const noexcept
    {
        return keys_pressed_[ToIndex(key_code)];
    }

    // Returns whether the key transitioned to released during the current frame.
    [[nodiscard]] bool WasReleasedThisFrame(KeyCode key_code) const noexcept
    {
        return keys_released_[ToIndex(key_code)];
    }

    // Sets the persistent down/up state for a key.
    void SetKeyDown(KeyCode key_code, bool down) noexcept
    {
        keys_down_[ToIndex(key_code)] = down;
    }

    // Marks a key as newly pressed for the current frame.
    void MarkPressed(KeyCode key_code) noexcept
    {
        keys_pressed_[ToIndex(key_code)] = true;
    }

    // Marks a key as newly released for the current frame.
    void MarkReleased(KeyCode key_code) noexcept
    {
        keys_released_[ToIndex(key_code)] = true;
    }

    // Clears only the per-frame pressed/released flags.
    void ClearTransient() noexcept
    {
        keys_pressed_.fill(false);
        keys_released_.fill(false);
    }

    // Clears both persistent and transient key state.
    void ClearAll() noexcept
    {
        keys_down_.fill(false);
        ClearTransient();
    }

  private:
    // Maps a key code to a safe array index, collapsing invalid values to index zero.
    [[nodiscard]] static constexpr std::size_t ToIndex(KeyCode key_code) noexcept
    {
        const auto index = static_cast<std::size_t>(static_cast<std::uint16_t>(key_code));
        return index < KeyCount ? index : 0;
    }

    std::array<bool, KeyCount> keys_down_{};
    std::array<bool, KeyCount> keys_pressed_{};
    std::array<bool, KeyCount> keys_released_{};
};
} 
