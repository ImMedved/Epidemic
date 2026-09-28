#pragma once

#include <cmath>
#include <type_traits>
#include <limits>
#include <optional>
#include <cstdint>

namespace epidemic::runtime
{
template <typename TValue>
[[nodiscard]] bool IsFinite(TValue value) noexcept
{
    static_assert(std::is_floating_point_v<TValue>);
    return std::isfinite(value);
}

template <typename TValue>
[[nodiscard]] bool IsFinitePositive(TValue value) noexcept
{
    return IsFinite(value) && value > static_cast<TValue>(0);
}

template <typename TValue>
[[nodiscard]] bool IsFiniteNonNegative(TValue value) noexcept
{
    return IsFinite(value) && value >= static_cast<TValue>(0);
}

template <typename TUnsigned>
[[nodiscard]] constexpr std::optional<TUnsigned> CheckedUnsignedIncrement(TUnsigned value) noexcept
{
    static_assert(std::is_integral_v<TUnsigned> && std::is_unsigned_v<TUnsigned>);
    if (value == std::numeric_limits<TUnsigned>::max())
    {
        return std::nullopt;
    }
    return static_cast<TUnsigned>(value + 1);
}

template <typename TUnsigned>
[[nodiscard]] constexpr std::optional<TUnsigned> CheckedUnsignedAdd(TUnsigned left, TUnsigned right) noexcept
{
    static_assert(std::is_integral_v<TUnsigned> && std::is_unsigned_v<TUnsigned>);
    if (right > std::numeric_limits<TUnsigned>::max() - left)
    {
        return std::nullopt;
    }
    return static_cast<TUnsigned>(left + right);
}

template <typename TUnsigned>
[[nodiscard]] constexpr std::optional<TUnsigned> CheckedRevisionIncrement(TUnsigned revision) noexcept
{
    return CheckedUnsignedIncrement(revision);
}

template <typename TUnsigned>
[[nodiscard]] constexpr std::optional<TUnsigned> CheckedGenerationIncrement(TUnsigned generation) noexcept
{
    return CheckedUnsignedIncrement(generation);
}

} // namespace epidemic::runtime
