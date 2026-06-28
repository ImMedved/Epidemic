#pragma once

#include <cstdint>
#include <functional>
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

    [[nodiscard]] constexpr std::uint64_t Packed() const noexcept
    {
        return (static_cast<std::uint64_t>(generation_) << 32u) | index_;
    }

    [[nodiscard]] constexpr bool operator==(const Handle &) const noexcept = default;

  private:
    std::uint32_t index_{kInvalidIndex};
    std::uint32_t generation_{0};
};
} // namespace epidemic::foundation

namespace std
{
template <typename TTag> struct hash<epidemic::foundation::Handle<TTag>>
{
    [[nodiscard]] size_t operator()(const epidemic::foundation::Handle<TTag> &value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Packed());
    }
};
} // namespace std