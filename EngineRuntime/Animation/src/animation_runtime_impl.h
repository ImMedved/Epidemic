#pragma once

#include "Epidemic/Runtime/Animation/animation_runtime.h"

#include <unordered_map>
#include <vector>

namespace epidemic::runtime::animation
{
// File note:
// Internal in-memory Animation runtime. It verifies resource/instance lifecycle and event flow without evaluating real poses.

class AnimationRuntime final : public ISkeletonRegistry,
                               public IAnimationClipRegistry,
                               public IAnimationRuntime,
                               public IPoseProvider,
                               public IAnimationEventBuffer
{
public:
    explicit AnimationRuntime(AnimationOptions options);

    [[nodiscard]] foundation::Result<void> RegisterSkeleton(SkeletonDesc desc) override;
    [[nodiscard]] bool HasSkeleton(SkeletonId id) const override;
    [[nodiscard]] foundation::Result<void> RegisterClip(AnimationClipDesc desc) override;
    [[nodiscard]] bool HasClip(AnimationClipId id) const override;

    [[nodiscard]] foundation::Result<AnimatorInstanceId> CreateAnimator(const AnimatorDesc& desc) override;
    [[nodiscard]] foundation::Result<AnimatorHandle> CreateAnimatorHandle(const AnimatorDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyAnimator(AnimatorInstanceId id) override;
    [[nodiscard]] foundation::Result<void> Play(AnimatorInstanceId id, AnimationClipId clip) override;
    [[nodiscard]] foundation::Result<void> Play(const AnimationPlaybackCommand& command) override;
    [[nodiscard]] foundation::Result<void> Pause(AnimatorHandle handle) override;
    [[nodiscard]] foundation::Result<void> Stop(AnimatorHandle handle) override;
    [[nodiscard]] foundation::Result<void> Crossfade(AnimatorHandle handle, AnimationClipId clip, GameDuration duration) override;
    [[nodiscard]] std::size_t Tick(std::size_t max_animators) override;
    [[nodiscard]] std::size_t Tick(GameDuration delta, std::size_t max_animators) override;
    [[nodiscard]] AnimatorState GetState(AnimatorInstanceId id) const override;
    [[nodiscard]] foundation::Result<void> SetLod(AnimatorInstanceId id, AnimationLodLevel lod) override;
    [[nodiscard]] PoseState GetPoseState(AnimatorInstanceId id) const override;
    [[nodiscard]] PoseSnapshot GetPoseSnapshot(AnimatorInstanceId id) const override;
    [[nodiscard]] PoseBuffer GetPoseBuffer(AnimatorHandle handle) const override;
    [[nodiscard]] std::span<const AnimationEvent> Events() const override;
    void Clear() override;

private:
    struct AnimatorRecord
    {
        AnimatorDesc desc{};
        AnimatorHandle handle{};
        AnimatorState state = AnimatorState::Ready;
        PoseState pose_state = PoseState::Clean;
        AnimationClipId playing_clip{};
        float local_time = 0.0f;
        std::uint64_t revision = 0;
    };

    [[nodiscard]] AnimatorRecord* FindAnimator(AnimatorInstanceId id);
    [[nodiscard]] const AnimatorRecord* FindAnimator(AnimatorInstanceId id) const;
    [[nodiscard]] AnimatorRecord* FindAnimator(AnimatorHandle handle);
    [[nodiscard]] const AnimatorRecord* FindAnimator(AnimatorHandle handle) const;
    [[nodiscard]] std::vector<AnimatorInstanceId> BuildAnimatorWorkList() const;
    void QueueEvent(AnimatorInstanceId animator, std::string name, float time);

    AnimationOptions options_{};
    std::uint64_t next_animator_value_ = 1;
    std::uint32_t next_generation_ = 1;
    std::unordered_map<SkeletonId, SkeletonDesc> skeletons_;
    std::unordered_map<AnimationClipId, AnimationClipDesc> clips_;
    std::unordered_map<AnimatorInstanceId, AnimatorRecord> animators_;
    std::vector<AnimationEvent> events_;
};
} // namespace epidemic::runtime::animation
