#pragma once

#include <compare>
#include <cstdint>

namespace epidemic::runtime
{
struct SchemaVersion
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    [[nodiscard]] constexpr auto operator<=>(const SchemaVersion&) const noexcept = default;
};
} // namespace epidemic::runtime
