#pragma once

#include <cstdint>

namespace epidemic::runtime
{
enum class ObjectProtectionFlags : std::uint32_t
{
    None = 0,
    PreventTheft = 1u << 0,
    PreventDecay = 1u << 1,
    PreventCleanup = 1u << 2,
    PreserveTransform = 1u << 3,
    PreserveCondition = 1u << 4,
};

[[nodiscard]] constexpr std::uint32_t ToProtectionMask(ObjectProtectionFlags flags) noexcept
{
    return static_cast<std::uint32_t>(flags);
}

[[nodiscard]] constexpr bool HasProtectionFlag(std::uint32_t mask, ObjectProtectionFlags flag) noexcept
{
    return (mask & ToProtectionMask(flag)) != 0u;
}

[[nodiscard]] constexpr std::uint32_t AddProtectionFlag(std::uint32_t mask, ObjectProtectionFlags flag) noexcept
{
    return mask | ToProtectionMask(flag);
}
} // namespace epidemic::runtime
