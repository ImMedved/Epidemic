#pragma once

namespace epidemic::runtime
{
enum class SceneNodeState
{
    Detached,
    Attached,
    Static,
    Dynamic,
    TransformDirty,
    BoundsDirty,
    VisibleCandidate,
    Hidden,
    Culled,
};
} // namespace epidemic::runtime
