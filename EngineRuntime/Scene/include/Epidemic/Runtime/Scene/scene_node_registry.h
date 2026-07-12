#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Scene/scene_node.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
class ISceneNodeRegistry
{
  public:
    // Function note: Handles ~iscene node registry.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~ISceneNodeRegistry() = default;

    // Function note: Creates node.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<SceneNodeId> CreateNode() = 0;
    // Function note: Destroys node.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> DestroyNode(SceneNodeId node) = 0;
    // Function note: Handles exists.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual bool Exists(SceneNodeId node) const = 0;
};
} 
