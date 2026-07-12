#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/runtime_budget.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_states.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
enum class MaterializationState
{
    NotMaterialized,
    Requested,
    Materializing,
    Materialized,
    Demoting,
    Demoted,
    Failed,
};

struct MaterializationRequest
{
    PersistentObjectId persistent_id{};
    ObjectRealityLevel target_reality = ObjectRealityLevel::Physical;
    RuntimeBudget budget{};

    [[nodiscard]] constexpr bool operator==(const MaterializationRequest&) const noexcept = default;
};

struct DemotionRequest
{
    RuntimeObjectId runtime_id{};
    ObjectRealityLevel target_reality = ObjectRealityLevel::Logical;

    [[nodiscard]] constexpr bool operator==(const DemotionRequest&) const noexcept = default;
};

class IObjectMaterializer
{
  public:
    // Function note: Handles ~iobject materializer.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~IObjectMaterializer() = default;

    // Function note: Handles materialize.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<RuntimeObjectId> Materialize(const MaterializationRequest& request) = 0;
    // Function note: Handles demote.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> Demote(const DemotionRequest& request) = 0;
};
} // namespace epidemic::runtime
