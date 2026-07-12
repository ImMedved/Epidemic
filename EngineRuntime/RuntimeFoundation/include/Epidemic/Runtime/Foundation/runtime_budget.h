#pragma once

#include <chrono>
#include <cstdint>

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

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
} 
