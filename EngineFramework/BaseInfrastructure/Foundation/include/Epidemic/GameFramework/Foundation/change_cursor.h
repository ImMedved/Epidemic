#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

namespace epidemic::gameplay
{
struct ChangeCursor
{
    std::uint64_t epoch = 0;
    std::uint64_t sequence = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return epoch != 0; }
    [[nodiscard]] constexpr ChangeCursor AtSequence(std::uint64_t value) const noexcept { return {epoch, value}; }
    [[nodiscard]] constexpr bool operator==(const ChangeCursor&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ChangeCursor&) const noexcept = default;
};

[[nodiscard]] constexpr std::optional<std::uint64_t> CheckedNextChangeEpoch(std::uint64_t epoch) noexcept
{
    if (epoch == std::numeric_limits<std::uint64_t>::max())
    {
        return std::nullopt;
    }
    return epoch + 1;
}
} // namespace epidemic::gameplay