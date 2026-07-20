#pragma once

#include <cstdint>

namespace epidemic::runtime
{
struct TimeScale
{
    std::int64_t numerator = 1;
    std::int64_t denominator = 1;

    [[nodiscard]] constexpr bool operator==(const TimeScale&) const noexcept = default;
};
} // namespace epidemic::runtime
