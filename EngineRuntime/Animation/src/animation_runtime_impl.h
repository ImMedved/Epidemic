#pragma once

#include "Epidemic/Runtime/Animation/animation_runtime.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace epidemic::runtime::animation
{
// Internal in-memory Animation runtime. It verifies resource/instance lifecycle and event flow without evaluating real poses.

class AnimationRuntime final : public ISkeletonRegistry,
                               public IAnimationClipRegistry,
                               public IAnimationRuntime,
                               public IPoseProvider,
                               public IAnimationEventBuffer
{
public:
    explicit AnimationRuntime(AnimationOptions options, AnimationDependencies dependencies = {});

    [[nodiscard]] foundation::Result<void> RegisterSkeleton(SkeletonDesc desc) override;
    [[nodiscard]] bool HasSkeleton(SkeletonId id) const override;
    [[nodiscard]] foundation::Result<void> RegisterClip(AnimationClipDesc desc) override;
    [[nodiscard]] bool HasClip(AnimationClipId id) const override;

    [[nodiscard]] foundation::Result<AnimatorHandle> CreateAnimatorHandle(const AnimatorDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyAnimator(AnimatorHandle handle) override;
    [[nodiscard]] foundation::Result<void> Play(const AnimationPlaybackCommand& command) override;
    [[nodiscard]] foundation::Result<void> Pause(AnimatorHandle handle) override;
    [[nodiscard]] foundation::Result<void> Stop(AnimatorHandle handle) override;
    [[nodiscard]] foundation::Result<void> Crossfade(AnimatorHandle handle, AnimationClipId clip, FrameDuration duration) override;
    [[nodiscard]] foundation::Result<std::size_t> Tick(FrameDuration delta, std::size_t max_animators) override;
    [[nodiscard]] foundation::Result<AnimatorSnapshot> GetAnimatorSnapshot(AnimatorHandle handle) const override;
    [[nodiscard]] foundation::Result<void> SetLod(AnimatorHandle handle, AnimationLodLevel lod) override;
    [[nodiscard]] foundation::Result<PoseState> GetPoseState(AnimatorHandle handle) const override;
    [[nodiscard]] foundation::Result<PoseSnapshot> GetPoseSnapshot(AnimatorHandle handle) const override;
    [[nodiscard]] foundation::Result<PoseBuffer> GetPoseBuffer(AnimatorHandle handle) const override;
    [[nodiscard]] std::span<const AnimationEvent> Events() const override;
    void Clear() override;

private:
    struct AnimatorRecord
    {
        AnimatorDesc desc{};
        AnimatorHandle handle{};
        AnimatorLifecycle lifecycle = AnimatorLifecycle::Ready;
        AnimatorReadiness readiness = AnimatorReadiness::Ready;
        AnimatorPlaybackState playback_state = AnimatorPlaybackState::Stopped;
        PoseState pose_state = PoseState::Clean;
        AnimatorPlayback playback{};
        std::optional<CrossfadeState> crossfade{};
        PoseBuffer cached_pose{};
        std::uint64_t revision = 0;
    };

    [[nodiscard]] foundation::Result<SkeletonDesc> ResolveSkeleton(SkeletonId id);
    [[nodiscard]] foundation::Result<AnimationClipDesc> ResolveClip(AnimationClipId id);
    [[nodiscard]] static AnimatorSnapshot ToSnapshot(const AnimatorRecord& animator) noexcept;
    [[nodiscard]] AnimatorRecord* FindAnimator(AnimatorHandle handle);
    [[nodiscard]] const AnimatorRecord* FindAnimator(AnimatorHandle handle) const;
    [[nodiscard]] std::vector<AnimatorInstanceId> BuildAnimatorWorkList() const;
    [[nodiscard]] PoseBuffer BuildPoseBuffer(const AnimatorRecord& animator) const;
    [[nodiscard]] foundation::Result<PoseBuffer> EvaluatePose(const AnimatorRecord& animator);
    [[nodiscard]] foundation::Result<void> PublishPose(const AnimatorRecord& animator);
    void AdvancePlayback(AnimatorRecord& animator, FrameDuration delta);
    void AdvanceCrossfade(AnimatorRecord& animator, FrameDuration delta);
    void QueueEvent(AnimatorInstanceId animator, std::string name, float time);

    AnimationOptions options_{};
    AnimationDependencies dependencies_{};
    std::uint64_t next_animator_value_ = 1;
    std::uint32_t next_generation_ = 1;
    std::unordered_map<SkeletonId, SkeletonDesc> skeletons_;
    std::unordered_map<AnimationClipId, AnimationClipDesc> clips_;
    std::unordered_map<AnimatorInstanceId, AnimatorRecord> animators_;
    std::vector<AnimationEvent> events_;
};
} // namespace epidemic::runtime::animation
