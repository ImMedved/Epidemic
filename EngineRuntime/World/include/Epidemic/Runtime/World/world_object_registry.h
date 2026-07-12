#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/World/world_object.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <optional>
#include <vector>

namespace epidemic::runtime
{
class IWorldObjectRegistry
{
  public:
    // Function note: Handles ~iworld object registry.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~IWorldObjectRegistry() = default;

    // Function note: Creates object.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<RuntimeObjectId> CreateObject(WorldObjectRecord record) = 0;
    // Function note: Destroys object.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> DestroyObject(RuntimeObjectId id) = 0;
    // Function note: Finds object.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::optional<WorldObjectRecord> FindObject(RuntimeObjectId id) const = 0;
    // Function note: Finds objects in chunk.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::vector<WorldObjectRecord> FindObjectsInChunk(ChunkId chunk) const = 0;
    // Function note: Sets placement.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> SetPlacement(RuntimeObjectId id, ObjectPlacement placement) = 0;
    // Function note: Sets residency.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> SetResidency(RuntimeObjectId id, ResidencyState state) = 0;
};
} 
