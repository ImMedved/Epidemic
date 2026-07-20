#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Animation/animation_types.h"

#include <memory>
#include <span>

namespace epidemic::runtime::animation
{
// Public contracts for registering animation resources, owning animator instances and exposing pose/event state.
// Implementations are allowed to be mock-backed while preserving lifecycle semantics.

class ISkeletonRegistry
{
public:
    virtual ~ISkeletonRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterSkeleton(SkeletonDesc desc) = 0;

    [[nodiscard]] virtual bool HasSkeleton(SkeletonId id) const = 0;
};

class IAnimationClipRegistry
{
public:
    virtual ~IAnimationClipRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterClip(AnimationClipDesc desc) = 0;

    [[nodiscard]] virtual bool HasClip(AnimationClipId id) const = 0;
};

class IAnimationRuntime
{
public:
    virtual ~IAnimationRuntime() = default;

    [[nodiscard]] virtual foundation::Result<AnimatorHandle> CreateAnimatorHandle(const AnimatorDesc& desc) = 0;

    [[nodiscard]] virtual foundation::Result<void> DestroyAnimator(AnimatorHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> Play(const AnimationPlaybackCommand& command) = 0;
    [[nodiscard]] virtual foundation::Result<void> Pause(AnimatorHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Stop(AnimatorHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Crossfade(AnimatorHandle handle, AnimationClipId clip, FrameDuration duration) = 0;

    [[nodiscard]] virtual foundation::Result<std::size_t> Tick(FrameDuration delta, std::size_t max_animators) = 0;

    [[nodiscard]] virtual foundation::Result<AnimatorSnapshot> GetAnimatorSnapshot(AnimatorHandle handle) const = 0;

    [[nodiscard]] virtual foundation::Result<void> SetLod(AnimatorHandle handle, AnimationLodLevel lod) = 0;
};

class IPoseProvider
{
public:
    virtual ~IPoseProvider() = default;

    [[nodiscard]] virtual foundation::Result<PoseState> GetPoseState(AnimatorHandle handle) const = 0;

    [[nodiscard]] virtual foundation::Result<PoseSnapshot> GetPoseSnapshot(AnimatorHandle handle) const = 0;
    [[nodiscard]] virtual foundation::Result<PoseBuffer> GetPoseBuffer(AnimatorHandle handle) const = 0;
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

class IAnimationEvaluatorBackend
{
public:
    virtual ~IAnimationEvaluatorBackend() = default;

    [[nodiscard]] virtual foundation::Result<PoseBuffer> EvaluatePose(const AnimationEvaluationRequest& request) const = 0;
};

class IAnimationEventBuffer
{
public:
    virtual ~IAnimationEventBuffer() = default;

    [[nodiscard]] virtual std::span<const AnimationEvent> Events() const = 0;

    virtual void Clear() = 0;
};

struct AnimationDependencies
{
    std::shared_ptr<IAnimationResourceSource> resources;
    std::shared_ptr<IAnimationPoseSink> pose_sink;
    std::shared_ptr<IAnimationEvaluatorBackend> evaluator;
};

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

