#pragma once

#include <cstdint>
#include <limits>

namespace epidemic::foundation
{
template <typename TTag> class Handle
{
  public:
    static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    constexpr Handle() noexcept = default;
    constexpr Handle(std::uint32_t index, std::uint32_t generation) noexcept : index_(index), generation_(generation)
    {
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index_ != kInvalidIndex;
    }

    [[nodiscard]] constexpr std::uint32_t Index() const noexcept
    {
        return index_;
    }

    [[nodiscard]] constexpr std::uint32_t Generation() const noexcept
    {
        return generation_;
    }

    [[nodiscard]] constexpr bool operator==(const Handle &) const noexcept = default;

  private:
    std::uint32_t index_{kInvalidIndex};
    std::uint32_t generation_{0};
};
} // namespace epidemic::foundation
