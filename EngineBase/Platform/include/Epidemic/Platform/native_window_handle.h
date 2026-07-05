#pragma once

namespace epidemic::platform
{
class NativeWindowHandle
{
  public:
    constexpr NativeWindowHandle() noexcept = default;
    explicit constexpr NativeWindowHandle(void *value) noexcept : value_(value)
    {
    }

    [[nodiscard]] constexpr void *Value() const noexcept
    {
        return value_;
    }

    template <typename T> [[nodiscard]] constexpr T As() const noexcept
    {
        return static_cast<T>(value_);
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value_ != nullptr;
    }

    [[nodiscard]] constexpr bool operator==(const NativeWindowHandle &) const noexcept = default;

  private:
    void *value_{nullptr};
};
} // namespace epidemic::platform
