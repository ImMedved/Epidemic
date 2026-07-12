#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <vector>

namespace epidemic::runtime
{
struct ResourceDependency
{
    ResourceId resource_id{};
    ResourceType type{};
    bool required = true;

    [[nodiscard]] constexpr bool operator==(const ResourceDependency&) const noexcept = default;
};

struct ResourceDependencySet
{
    ResourceId root{};
    std::vector<ResourceDependency> dependencies;
};
} 
