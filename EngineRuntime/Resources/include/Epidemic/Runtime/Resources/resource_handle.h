#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include <cstdint>

namespace epidemic::runtime
{
struct ResourceHandle
{
    ResourceId id{};
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return id.IsValid() && generation != 0;
    }

    [[nodiscard]] constexpr bool operator==(const ResourceHandle&) const noexcept = default;
};

using ResourceAcquisitionId = std::uint64_t;

struct ResourceLease
{
    ResourceHandle resource{};
    ResourceAcquisitionId acquisition = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return resource.IsValid() && acquisition != 0;
    }

    [[nodiscard]] constexpr bool operator==(const ResourceLease&) const noexcept = default;
};
} 
