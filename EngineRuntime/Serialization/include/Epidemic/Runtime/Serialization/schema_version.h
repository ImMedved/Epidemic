#pragma once

#include <compare>
#include <cstdint>

// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
struct SchemaVersion
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    [[nodiscard]] constexpr auto operator<=>(const SchemaVersion&) const noexcept = default;
};
} 
