#pragma once

#include "Epidemic/Runtime/World/world_object.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <optional>
#include <vector>

namespace epidemic::runtime
{
class IWorldQuery
{
  public:
    // Function note: Handles ~iworld query.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~IWorldQuery() = default;

    // Function note: Finds object.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::optional<WorldObjectRecord> FindObject(RuntimeObjectId id) const = 0;
    // Function note: Finds objects in region.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::vector<WorldObjectRecord> FindObjectsInRegion(RegionId region) const = 0;
    // Function note: Finds objects in chunk.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::vector<WorldObjectRecord> FindObjectsInChunk(ChunkId chunk) const = 0;
    // Function note: Finds objects by reality.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::vector<WorldObjectRecord> FindObjectsByReality(ObjectRealityLevel reality) const = 0;
};
} 
