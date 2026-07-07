#pragma once

#include <chrono>
#include <cstdint>

namespace epidemic::runtime
{
struct RuntimeBudget
{
    std::chrono::microseconds max_time{};
    std::uint32_t max_items = 0;
    std::uint64_t max_bytes = 0;

    [[nodiscard]] constexpr bool HasTimeBudget() const noexcept
    {
        return max_time.count() > 0;
    }

    [[nodiscard]] constexpr bool HasItemBudget() const noexcept
    {
        return max_items != 0;
    }

    [[nodiscard]] constexpr bool HasByteBudget() const noexcept
    {
        return max_bytes != 0;
    }

    [[nodiscard]] constexpr bool IsEmpty() const noexcept
    {
        return !HasTimeBudget() && !HasItemBudget() && !HasByteBudget();
    }
};
} // namespace epidemic::runtime
