#pragma once

namespace epidemic::platform
{
// This file defines the opaque carrier used to pass native window handles across module boundaries.
// The wrapper keeps platform-specific pointer types out of higher-level headers.

class NativeWindowHandle
{
  public:
    // Creates an empty handle.
    constexpr NativeWindowHandle() noexcept = default;

    // Wraps a raw native window pointer value.
    explicit constexpr NativeWindowHandle(void *value) noexcept : value_(value)
    {
    }

    // Returns the wrapped raw pointer value.
    [[nodiscard]] constexpr void *Value() const noexcept
    {
        return value_;
    }

    // Returns the wrapped raw pointer cast to the requested native handle type.
    template <typename T> [[nodiscard]] constexpr T As() const noexcept
    {
        return static_cast<T>(value_);
    }

    // Returns whether the handle contains a non-null native pointer.
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value_ != nullptr;
    }

    // Compares two wrapped native handle values.
    [[nodiscard]] constexpr bool operator==(const NativeWindowHandle &) const noexcept = default;

  private:
    void *value_{nullptr};
};
} // namespace epidemic::platform