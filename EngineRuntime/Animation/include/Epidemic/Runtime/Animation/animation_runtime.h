#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Animation/animation_types.h"

#include <memory>
#include <span>

namespace epidemic::runtime::animation
{
// File note:
// Public contracts for registering animation resources, owning animator instances and exposing pose/event state.
// Implementations are allowed to be mock-backed while preserving lifecycle semantics.

class ISkeletonRegistry
{
public:
    virtual ~ISkeletonRegistry() = default;

    // Function note: Registers skeleton metadata for animator creation.
    // Inputs: skeleton descriptor; outputs: success/failure Result.
    // Relations: CreateAnimator validates skeleton ids through this registry.
    [[nodiscard]] virtual foundation::Result<void> RegisterSkeleton(SkeletonDesc desc) = 0;

    // Function note: Checks whether a skeleton exists.
    // Inputs: skeleton id; outputs: true when registered.
    // Relations: used by animator lifecycle validation.
    [[nodiscard]] virtual bool HasSkeleton(SkeletonId id) const = 0;
};

class IAnimationClipRegistry
{
public:
    virtual ~IAnimationClipRegistry() = default;

    // Function note: Registers clip metadata for playback.
    // Inputs: clip descriptor; outputs: success/failure Result.
    // Relations: Play validates clip existence and skeleton compatibility through this registry.
    [[nodiscard]] virtual foundation::Result<void> RegisterClip(AnimationClipDesc desc) = 0;

    // Function note: Checks whether a clip exists.
    // Inputs: clip id; outputs: true when registered.
    // Relations: cheap preflight for animation playback systems.
    [[nodiscard]] virtual bool HasClip(AnimationClipId id) const = 0;
};

class IAnimationRuntime
{
public:
    virtual ~IAnimationRuntime() = default;

    // Function note: Creates an animator instance bound to a runtime object and skeleton.
    // Inputs: owner/skeleton/lod descriptor; outputs: animator id or validation error.
    // Relations: Play, DestroyAnimator and pose queries operate on returned ids.
    [[nodiscard]] virtual foundation::Result<AnimatorInstanceId> CreateAnimator(const AnimatorDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<AnimatorHandle> CreateAnimatorHandle(const AnimatorDesc& desc) = 0;

    // Function note: Destroys an animator instance.
    // Inputs: animator id; outputs: success/failure Result.
    // Relations: removes pose/event ownership for the instance.
    [[nodiscard]] virtual foundation::Result<void> DestroyAnimator(AnimatorInstanceId id) = 0;

    // Function note: Starts playback for a registered clip.
    // Inputs: animator id and clip id; outputs: success/failure Result.
    // Relations: transitions state to Playing and queues a generic animation event.
    [[nodiscard]] virtual foundation::Result<void> Play(AnimatorInstanceId id, AnimationClipId clip) = 0;
    [[nodiscard]] virtual foundation::Result<void> Play(const AnimationPlaybackCommand& command) = 0;
    [[nodiscard]] virtual foundation::Result<void> Pause(AnimatorHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Stop(AnimatorHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Crossfade(AnimatorHandle handle, AnimationClipId clip, GameDuration duration) = 0;

    // Function note: Advances animator playback and mock pose evaluation by budgeted items.
    // Inputs: maximum number of animators to advance; outputs: number of transitioned animators.
    // Relations: emits finish events for non-looping mock clips.
    [[nodiscard]] virtual std::size_t Tick(std::size_t max_animators) = 0;
    [[nodiscard]] virtual std::size_t Tick(GameDuration delta, std::size_t max_animators) = 0;

    // Function note: Reads animator lifecycle state.
    // Inputs: animator id; outputs: state, Disabled for unknown ids.
    // Relations: lets consumers avoid touching internal animator records.
    [[nodiscard]] virtual AnimatorState GetState(AnimatorInstanceId id) const = 0;

    // Function note: Updates the LOD hint for an animator.
    // Inputs: animator id and LOD level; outputs: success/failure Result.
    // Relations: LOD affects pose state but does not encode higher-level actor behavior.
    [[nodiscard]] virtual foundation::Result<void> SetLod(AnimatorInstanceId id, AnimationLodLevel lod) = 0;
};

class IPoseProvider
{
public:
    virtual ~IPoseProvider() = default;

    // Function note: Reads pose readiness for an animator.
    // Inputs: animator id; outputs: pose state for renderer-facing adapters.
    // Relations: Renderer may consume pose state through a public adapter, but does not own Animation runtime.
    [[nodiscard]] virtual PoseState GetPoseState(AnimatorInstanceId id) const = 0;

    // Function note: Reads an immutable pose snapshot with revision.
    // Inputs: animator id; outputs: pose snapshot for renderer-facing adapters.
    // Relations: avoids exposing mutable animator internals to Renderer or gameplay systems.
    [[nodiscard]] virtual PoseSnapshot GetPoseSnapshot(AnimatorInstanceId id) const = 0;
    [[nodiscard]] virtual PoseBuffer GetPoseBuffer(AnimatorHandle handle) const = 0;
};

class IAnimationResourceSource
{
public:
    virtual ~IAnimationResourceSource() = default;

    [[nodiscard]] virtual foundation::Result<SkeletonDesc> LoadSkeleton(SkeletonId id) const = 0;
    [[nodiscard]] virtual foundation::Result<AnimationClipDesc> LoadClip(AnimationClipId id) const = 0;
};

class IAnimationPoseSink
{
public:
    virtual ~IAnimationPoseSink() = default;

    [[nodiscard]] virtual foundation::Result<void> Publish(std::shared_ptr<const PoseBuffer> pose) = 0;
};

class IAnimationEventBuffer
{
public:
    virtual ~IAnimationEventBuffer() = default;

    // Function note: Exposes queued generic animation events.
    // Inputs: none; outputs: immutable span over current event buffer.
    // Relations: systems can consume animation notifications without direct callbacks.
    [[nodiscard]] virtual std::span<const AnimationEvent> Events() const = 0;

    // Function note: Clears queued events after consumers process them.
    // Inputs: none; outputs: none.
    // Relations: called by frame orchestration after event dispatch.
    virtual void Clear() = 0;
};

struct AnimationDependencies
{
    std::shared_ptr<IAnimationResourceSource> resources;
    std::shared_ptr<IAnimationPoseSink> pose_sink;
};

[[nodiscard]] std::unique_ptr<class AnimationRuntime> CreateAnimationRuntime(
    AnimationOptions options = {},
    AnimationDependencies dependencies = {});

struct AnimationServices
{
    std::shared_ptr<ISkeletonRegistry> skeletons;
    std::shared_ptr<IAnimationClipRegistry> clips;
    std::shared_ptr<IAnimationRuntime> runtime;
    std::shared_ptr<IPoseProvider> poses;
    std::shared_ptr<IAnimationEventBuffer> events;
};

[[nodiscard]] foundation::Result<AnimationServices> CreateAnimationServices(
    AnimationOptions options = {},
    AnimationDependencies dependencies = {});
[[nodiscard]] AnimationServices CreateReferenceAnimationServices(AnimationOptions options = {});
[[nodiscard]] AnimationServices CreateMockAnimationServices(AnimationOptions options = {});
} // namespace epidemic::runtime::animation

