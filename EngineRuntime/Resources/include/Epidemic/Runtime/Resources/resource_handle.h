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
        return id.IsValid();
    }

    [[nodiscard]] constexpr bool operator==(const ResourceHandle&) const noexcept = default;
};
} 
