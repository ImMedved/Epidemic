#pragma once

#include "Epidemic/Runtime/Foundation/runtime_budget.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
struct ResourceRequest
{
    ResourceId resource_id{};
    ResourceType type{};
    RuntimeBudget budget_hint{};
};
} 
