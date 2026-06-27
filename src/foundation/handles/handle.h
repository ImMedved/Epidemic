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
    // Stores an index plus generation pair for safe indirect references.
    constexpr Handle(std::uint32_t index, std::uint32_t generation) noexcept : index_(index), generation_(generation)
    {
    }

    // Reports whether the handle points at a meaningful slot.
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index_ != kInvalidIndex;
    }

    // Returns the slot index portion of the handle.
    [[nodiscard]] constexpr std::uint32_t Index() const noexcept
    {
        return index_;
    }

    // Returns the generation used to detect stale references.
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
