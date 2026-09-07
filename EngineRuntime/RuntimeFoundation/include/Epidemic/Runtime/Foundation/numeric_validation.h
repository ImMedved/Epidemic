#pragma once

#include <cmath>
#include <type_traits>

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
} // namespace epidemic::runtime
