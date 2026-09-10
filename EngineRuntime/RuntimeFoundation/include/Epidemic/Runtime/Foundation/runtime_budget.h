#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/result.h"

namespace epidemic::runtime
{
// Zero values mean unlimited/no constraint. RuntimeBudget is a compact value object,
// not a scheduler; individual majors decide how to spend the allowed work.
struct RuntimeBudget
{
    std::chrono::microseconds max_time{};
    std::uint32_t max_items = 0;
    std::uint64_t max_bytes = 0;

    [[nodiscard]] constexpr bool HasTimeLimit() const noexcept { return max_time.count() > 0; }
    [[nodiscard]] constexpr bool HasItemLimit() const noexcept { return max_items != 0; }
    [[nodiscard]] constexpr bool HasByteLimit() const noexcept { return max_bytes != 0; }

    [[nodiscard]] constexpr bool HasTimeBudget() const noexcept { return HasTimeLimit(); }
    [[nodiscard]] constexpr bool HasItemBudget() const noexcept { return HasItemLimit(); }
    [[nodiscard]] constexpr bool HasByteBudget() const noexcept { return HasByteLimit(); }

    [[nodiscard]] constexpr bool IsUnlimited() const noexcept
    {
        return !HasTimeLimit() && !HasItemLimit() && !HasByteLimit();
    }
};


[[nodiscard]] inline foundation::Result<void> ValidateRuntimeBudget(const RuntimeBudget& budget)
{
    if (budget.max_time.count() < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime.invalid_budget", "runtime time budget must not be negative"));
    }
    return foundation::Result<void>::Success();
}

struct RuntimeBudgetConsumption
{
    std::size_t processed_items = 0;
    std::size_t processed_bytes = 0;
    std::chrono::microseconds elapsed{};
};
} // namespace epidemic::runtime
