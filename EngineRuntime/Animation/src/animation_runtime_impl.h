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
    [[nodiscard]] foundation::Result<void> DestroyAnimator(AnimatorInstanceId id) override;
    [[nodiscard]] foundation::Result<void> Play(AnimatorInstanceId id, AnimationClipId clip) override;
    [[nodiscard]] std::size_t Tick(std::size_t max_animators) override;
    [[nodiscard]] AnimatorState GetState(AnimatorInstanceId id) const override;
    [[nodiscard]] foundation::Result<void> SetLod(AnimatorInstanceId id, AnimationLodLevel lod) override;
    [[nodiscard]] PoseState GetPoseState(AnimatorInstanceId id) const override;
    [[nodiscard]] std::span<const AnimationEvent> Events() const override;
    void Clear() override;

private:
    struct AnimatorRecord
    {
        AnimatorDesc desc{};
        AnimatorState state = AnimatorState::Ready;
        PoseState pose_state = PoseState::Clean;
        AnimationClipId playing_clip{};
        float local_time = 0.0f;
    };

    [[nodiscard]] AnimatorRecord* FindAnimator(AnimatorInstanceId id);
    [[nodiscard]] const AnimatorRecord* FindAnimator(AnimatorInstanceId id) const;
    void QueueEvent(AnimatorInstanceId animator, std::string name, float time);

    AnimationOptions options_{};
    std::uint64_t next_animator_value_ = 1;
    std::unordered_map<SkeletonId, SkeletonDesc> skeletons_;
    std::unordered_map<AnimationClipId, AnimationClipDesc> clips_;
    std::unordered_map<AnimatorInstanceId, AnimatorRecord> animators_;
    std::vector<AnimationEvent> events_;
};
} // namespace epidemic::runtime::animation
