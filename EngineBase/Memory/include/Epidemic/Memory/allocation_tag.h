#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace epidemic::memory
{
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

[[nodiscard]] constexpr std::size_t AllocationTagCount() noexcept
{
    return static_cast<std::size_t>(AllocationTag::Count);
}

[[nodiscard]] constexpr bool IsKnownAllocationTag(AllocationTag tag) noexcept
{
    return static_cast<std::size_t>(tag) < static_cast<std::size_t>(AllocationTag::Count);
}

[[nodiscard]] constexpr AllocationTag NormalizeAllocationTag(AllocationTag tag) noexcept
{
    return IsKnownAllocationTag(tag) ? tag : AllocationTag::Unknown;
}

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