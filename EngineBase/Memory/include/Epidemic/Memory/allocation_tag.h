#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace epidemic::memory
{
// This file defines the coarse allocation categories tracked by the EngineBase memory layer.
// Tags are used by IMemoryTracker, allocators, diagnostics, and future budget-based checks.

enum class AllocationTag : std::uint8_t
{
    Core,
    Platform,
    Input,
    RHI,
    D3D11,
    Diagnostics,
    Tests,
    Unknown,
    Count,
};

// Returns the number of valid accounting tags.
[[nodiscard]] constexpr std::size_t AllocationTagCount() noexcept
{
    return static_cast<std::size_t>(AllocationTag::Count);
}

// Returns true when the supplied enum value is inside the known tag range.
[[nodiscard]] constexpr bool IsKnownAllocationTag(AllocationTag tag) noexcept
{
    return static_cast<std::size_t>(tag) < static_cast<std::size_t>(AllocationTag::Count);
}

// Maps invalid or out-of-range values to Unknown.
// Relationship: used by trackers so corrupted or unchecked input does not escape array bounds.
[[nodiscard]] constexpr AllocationTag NormalizeAllocationTag(AllocationTag tag) noexcept
{
    return IsKnownAllocationTag(tag) ? tag : AllocationTag::Unknown;
}

// Returns a stable text name for diagnostics and logs.
[[nodiscard]] inline std::string_view ToString(AllocationTag tag) noexcept
{
    switch (NormalizeAllocationTag(tag))
    {
    case AllocationTag::Core:
        return "Core";
    case AllocationTag::Platform:
        return "Platform";
    case AllocationTag::Input:
        return "Input";
    case AllocationTag::RHI:
        return "RHI";
    case AllocationTag::D3D11:
        return "D3D11";
    case AllocationTag::Diagnostics:
        return "Diagnostics";
    case AllocationTag::Tests:
        return "Tests";
    case AllocationTag::Unknown:
    case AllocationTag::Count:
        return "Unknown";
    }

    return "Unknown";
}
} // namespace epidemic::memory