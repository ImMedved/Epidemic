#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Scene/bounds.h"
#include "Epidemic/Runtime/Scene/scene_node.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <optional>

namespace epidemic::runtime
{
class ISpatialIndex
{
  public:
    // Function note: Handles ~ispatial index.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~ISpatialIndex() = default;

    // Function note: Sets bounds.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> SetBounds(SceneNodeId node, const Aabb& bounds) = 0;
    // Function note: Gets bounds.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::optional<Aabb> GetBounds(SceneNodeId node) const = 0;
    // Function note: Marks bounds clean.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual void MarkBoundsClean(SceneNodeId node) = 0;
    // Function note: Checks bounds dirty.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual bool IsBoundsDirty(SceneNodeId node) const = 0;
};
} 
