#include "animation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime::animation
{
AnimationRuntime::AnimationRuntime(AnimationOptions options) : options_(options)
{
}

foundation::Result<void> AnimationRuntime::RegisterSkeleton(SkeletonDesc desc)
{
    if (!desc.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_skeleton", "skeleton id must be valid before registration"));
    }

    if (desc.joint_count == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.empty_skeleton", "skeleton must contain at least one joint"));
    }

    skeletons_[desc.id] = desc;
    return foundation::Result<void>::Success();
}

bool AnimationRuntime::HasSkeleton(SkeletonId id) const
{
    return skeletons_.contains(id);
}

foundation::Result<void> AnimationRuntime::RegisterClip(AnimationClipDesc desc)
{
    if (!desc.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_clip", "clip id must be valid before registration"));
    }

    if (!HasSkeleton(desc.skeleton))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.skeleton_not_found", "clip must reference a registered skeleton"));
    }

    if (desc.duration_seconds <= 0.0f)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_clip_duration", "clip duration must be positive"));
    }

    clips_[desc.id] = desc;
    return foundation::Result<void>::Success();
}

bool AnimationRuntime::HasClip(AnimationClipId id) const
{
    return clips_.contains(id);
}

foundation::Result<AnimatorInstanceId> AnimationRuntime::CreateAnimator(const AnimatorDesc& desc)
{
    if (!desc.owner.IsValid())
    {
        return foundation::Result<AnimatorInstanceId>::Failure(
            foundation::Error::Create("animation.invalid_owner", "animator owner must be valid before creation"));
    }

    if (!HasSkeleton(desc.skeleton))
    {
        return foundation::Result<AnimatorInstanceId>::Failure(
            foundation::Error::Create("animation.skeleton_not_found", "animator must reference a registered skeleton"));
    }

    const AnimatorInstanceId id{next_animator_value_++};
    AnimatorRecord record{};
    record.desc = desc;
    record.state = AnimatorState::Ready;
    record.pose_state = PoseState::Clean;
    record.revision = 1;
    animators_.emplace(id, record);
    return foundation::Result<AnimatorInstanceId>::Success(id);
}

foundation::Result<void> AnimationRuntime::DestroyAnimator(AnimatorInstanceId id)
{
    AnimatorRecord* animator = FindAnimator(id);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator was not found for destruction"));
    }

    animators_.erase(id);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::Play(AnimatorInstanceId id, AnimationClipId clip)
{
    AnimatorRecord* animator = FindAnimator(id);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator was not found for playback"));
    }

    const auto clip_it = clips_.find(clip);
    if (clip_it == clips_.end())
    {
        animator->state = AnimatorState::ResourceMissing;
        ++animator->revision;
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.clip_not_found", "animation clip was not registered"));
    }

    if (clip_it->second.skeleton != animator->desc.skeleton)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.skeleton_mismatch", "clip skeleton does not match animator skeleton"));
    }

    animator->playing_clip = clip;
    animator->local_time = 0.0f;
    animator->state = AnimatorState::Playing;
    animator->pose_state = options_.enable_mock_pose_evaluation ? PoseState::Evaluating : PoseState::Dirty;
    ++animator->revision;
    QueueEvent(id, "animation.started", 0.0f);
    return foundation::Result<void>::Success();
}

std::size_t AnimationRuntime::Tick(std::size_t max_animators)
{
    const std::size_t limit = max_animators == 0 ? animators_.size() : max_animators;
    std::size_t transitioned = 0;

    for (const AnimatorInstanceId id : BuildAnimatorWorkList())
    {
        if (transitioned >= limit)
        {
            break;
        }

        AnimatorRecord& animator = animators_[id];
        if (animator.state == AnimatorState::Playing || animator.state == AnimatorState::Blending)
        {
            animator.local_time += 1.0f;
            animator.pose_state = animator.desc.lod == AnimationLodLevel::Frozen ? PoseState::Clean : PoseState::Ready;
            const auto clip_it = clips_.find(animator.playing_clip);
            if (clip_it != clips_.end() && !clip_it->second.loop && animator.local_time >= clip_it->second.duration_seconds)
            {
                animator.state = AnimatorState::Finished;
                QueueEvent(id, "animation.finished", animator.local_time);
            }
            ++animator.revision;
            ++transitioned;
        }
    }

    return transitioned;
}

AnimatorState AnimationRuntime::GetState(AnimatorInstanceId id) const
{
    const AnimatorRecord* animator = FindAnimator(id);
    if (animator == nullptr)
    {
        return AnimatorState::Disabled;
    }

    return animator->state;
}

foundation::Result<void> AnimationRuntime::SetLod(AnimatorInstanceId id, AnimationLodLevel lod)
{
    AnimatorRecord* animator = FindAnimator(id);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator was not found for LOD update"));
    }

    animator->desc.lod = lod;
    animator->pose_state = lod == AnimationLodLevel::Frozen ? PoseState::Clean : PoseState::Dirty;
    ++animator->revision;
    return foundation::Result<void>::Success();
}

PoseState AnimationRuntime::GetPoseState(AnimatorInstanceId id) const
{
    const AnimatorRecord* animator = FindAnimator(id);
    if (animator == nullptr)
    {
        return PoseState::Clean;
    }

    return animator->pose_state;
}

PoseSnapshot AnimationRuntime::GetPoseSnapshot(AnimatorInstanceId id) const
{
    const AnimatorRecord* animator = FindAnimator(id);
    if (animator == nullptr)
    {
        return PoseSnapshot{id, PoseState::Clean, AnimationLodLevel::Frozen, 0};
    }

    return PoseSnapshot{id, animator->pose_state, animator->desc.lod, animator->revision};
}

std::span<const AnimationEvent> AnimationRuntime::Events() const
{
    return events_;
}

void AnimationRuntime::Clear()
{
    events_.clear();
}

AnimationRuntime::AnimatorRecord* AnimationRuntime::FindAnimator(AnimatorInstanceId id)
{
    const auto iterator = animators_.find(id);
    if (iterator == animators_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const AnimationRuntime::AnimatorRecord* AnimationRuntime::FindAnimator(AnimatorInstanceId id) const
{
    const auto iterator = animators_.find(id);
    if (iterator == animators_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

std::vector<AnimatorInstanceId> AnimationRuntime::BuildAnimatorWorkList() const
{
    std::vector<AnimatorInstanceId> work_list;
    work_list.reserve(animators_.size());
    for (const auto& [id, animator] : animators_)
    {
        if (animator.state == AnimatorState::Playing || animator.state == AnimatorState::Blending)
        {
            work_list.push_back(id);
        }
    }

    std::sort(work_list.begin(), work_list.end(), [](AnimatorInstanceId left, AnimatorInstanceId right) {
        return left.value < right.value;
    });
    return work_list;
}

void AnimationRuntime::QueueEvent(AnimatorInstanceId animator, std::string name, float time)
{
    events_.push_back(AnimationEvent{animator, std::move(name), time});
}
} // namespace epidemic::runtime::animation
