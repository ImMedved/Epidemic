#pragma once

namespace epidemic::rhi
{
class PresentationSurfaceHandle
{
  public:
    constexpr PresentationSurfaceHandle() noexcept = default;
    explicit constexpr PresentationSurfaceHandle(void *value) noexcept : value_(value)
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

    [[nodiscard]] constexpr bool operator==(const PresentationSurfaceHandle &) const noexcept = default;

  private:
    void *value_{nullptr};
};
} // namespace epidemic::rhi
