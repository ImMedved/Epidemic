#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Resources/resource_type.h"


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
