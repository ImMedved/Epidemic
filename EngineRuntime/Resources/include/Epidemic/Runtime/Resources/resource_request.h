#pragma once

#include "Epidemic/Runtime/Foundation/runtime_budget.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Resources/resource_type.h"
namespace epidemic::runtime
{
struct ResourceRequest
{
    ResourceId resource_id{};
    ResourceType type{};
    RuntimeBudget budget_hint{};
};
} 
