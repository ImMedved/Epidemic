#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

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
} // namespace epidemic::runtime
