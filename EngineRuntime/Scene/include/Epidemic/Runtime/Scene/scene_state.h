#pragma once


// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.
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
} 
