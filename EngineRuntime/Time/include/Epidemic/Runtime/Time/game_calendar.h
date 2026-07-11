#pragma once

#include <cstdint>

namespace epidemic::runtime
{
struct CalendarDate
{
    std::int32_t year = 1;
    std::uint32_t day_of_year = 1;
    std::uint32_t hour = 0;
    std::uint32_t minute = 0;

    [[nodiscard]] constexpr bool operator==(const CalendarDate&) const noexcept = default;
};
} // namespace epidemic::runtime
