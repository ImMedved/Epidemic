#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Scene/scene_node.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
enum class ReparentMode
{
    KeepWorld,
    KeepLocal,
};

// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class ISceneNodeRegistry
{
  public:
    virtual ~ISceneNodeRegistry() = default;

    [[nodiscard]] virtual foundation::Result<SceneNodeId> CreateNode() = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyNode(SceneNodeId node) = 0;
    [[nodiscard]] virtual bool Exists(SceneNodeId node) const = 0;
    [[nodiscard]] virtual std::optional<SceneNode> GetNode(SceneNodeId node) const = 0;
    [[nodiscard]] virtual foundation::Result<void> AttachNode(SceneNodeId child, SceneNodeId parent, ReparentMode mode = ReparentMode::KeepWorld) = 0;
    [[nodiscard]] virtual foundation::Result<void> DetachNode(SceneNodeId child, ReparentMode mode = ReparentMode::KeepWorld) = 0;
    [[nodiscard]] virtual std::optional<SceneNodeId> GetParent(SceneNodeId child) const = 0;
    [[nodiscard]] virtual std::vector<SceneNodeId> GetChildren(SceneNodeId parent) const = 0;
    [[nodiscard]] virtual foundation::Result<void> SetMobility(SceneNodeId node, SceneMobility mobility) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetVisibility(SceneNodeId node, SceneVisibilityState visibility) = 0;
    [[nodiscard]] virtual std::uint64_t GetRevision() const = 0;
};
} // namespace epidemic::runtime
