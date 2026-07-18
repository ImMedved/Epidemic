#include "animation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace epidemic::runtime::animation
{
AnimationRuntime::AnimationRuntime(AnimationOptions options, AnimationDependencies dependencies)
    : options_(options), dependencies_(std::move(dependencies))
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
    return skeletons_.contains(id) || dependencies_.resources != nullptr;
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
    return clips_.contains(id) || dependencies_.resources != nullptr;
}

foundation::Result<AnimatorInstanceId> AnimationRuntime::CreateAnimator(const AnimatorDesc& desc)
{
    if (!desc.owner.IsValid())
    {
        return foundation::Result<AnimatorInstanceId>::Failure(
            foundation::Error::Create("animation.invalid_owner", "animator owner must be valid before creation"));
    }

    const auto skeleton = ResolveSkeleton(desc.skeleton);
    if (!skeleton)
    {
        return foundation::Result<AnimatorInstanceId>::Failure(
            skeleton.GetError());
    }

    const AnimatorInstanceId id{next_animator_value_++};
    const AnimatorHandle handle{id, next_generation_++};
    AnimatorRecord record{};
    record.desc = desc;
    record.handle = handle;
    record.lifecycle = AnimatorLifecycle::Ready;
    record.readiness = AnimatorReadiness::Ready;
    record.playback_state = AnimatorPlaybackState::Stopped;
    record.pose_state = PoseState::Clean;
    record.revision = 1;
    animators_.emplace(id, record);
    return foundation::Result<AnimatorInstanceId>::Success(id);
}

foundation::Result<AnimatorHandle> AnimationRuntime::CreateAnimatorHandle(const AnimatorDesc& desc)
{
    const auto id = CreateAnimator(desc);
    if (!id)
    {
        return foundation::Result<AnimatorHandle>::Failure(id.GetError());
    }

    const AnimatorRecord* animator = FindAnimator(id.Value());
    return foundation::Result<AnimatorHandle>::Success(animator->handle);
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

    const auto clip_result = ResolveClip(clip);
    if (!clip_result)
    {
        animator->readiness = AnimatorReadiness::ResourceMissing;
        ++animator->revision;
        return foundation::Result<void>::Failure(
            clip_result.GetError());
    }

    if (clip_result.Value().skeleton != animator->desc.skeleton)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.skeleton_mismatch", "clip skeleton does not match animator skeleton"));
    }

    animator->readiness = AnimatorReadiness::Ready;
    animator->playback = AnimatorPlayback{clip, false, 1.0, GameDuration{}};
    animator->crossfade.reset();
    animator->playback_state = AnimatorPlaybackState::Playing;
    animator->pose_state = options_.enable_mock_pose_evaluation ? PoseState::Evaluating : PoseState::Dirty;
    ++animator->revision;
    QueueEvent(id, "animation.started", 0.0f);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::Play(const AnimationPlaybackCommand& command)
{
    AnimatorRecord* animator = FindAnimator(command.animator);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for playback"));
    }

    const auto result = Play(command.animator.id, command.clip);
    if (result)
    {
        AnimatorRecord* refreshed = FindAnimator(command.animator);
        if (refreshed != nullptr)
        {
            refreshed->playback.loop = command.loop;
        }
    }
    return result;
}

foundation::Result<void> AnimationRuntime::Pause(AnimatorHandle handle)
{
    AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pause"));
    }

    if (animator->playback_state != AnimatorPlaybackState::Paused)
    {
        animator->playback_state = AnimatorPlaybackState::Paused;
        ++animator->revision;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::Stop(AnimatorHandle handle)
{
    AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for stop"));
    }

    if (animator->playback_state != AnimatorPlaybackState::Stopped)
    {
        animator->playback_state = AnimatorPlaybackState::Stopped;
        animator->playback.local_time = GameDuration{};
        animator->crossfade.reset();
        animator->pose_state = PoseState::Clean;
        ++animator->revision;
        QueueEvent(handle.id, "animation.stopped", 0.0f);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::Crossfade(AnimatorHandle handle, AnimationClipId clip, GameDuration duration)
{
    if (duration.ticks < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_fade", "crossfade duration must not be negative"));
    }

    AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for crossfade"));
    }

    const auto clip_result = ResolveClip(clip);
    if (!clip_result)
    {
        animator->readiness = AnimatorReadiness::ResourceMissing;
        ++animator->revision;
        return foundation::Result<void>::Failure(clip_result.GetError());
    }

    if (clip_result.Value().skeleton != animator->desc.skeleton)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.skeleton_mismatch", "clip skeleton does not match animator skeleton"));
    }

    if (!animator->playback.clip.IsValid())
    {
        const auto play = Play(AnimationPlaybackCommand{handle, clip, false, duration});
        if (!play)
        {
            return play;
        }
        animator = FindAnimator(handle);
    }

    if (duration.ticks == 0)
    {
        animator->playback = AnimatorPlayback{clip, false, 1.0, GameDuration{}};
        animator->playback_state = AnimatorPlaybackState::Playing;
        animator->crossfade.reset();
        ++animator->revision;
        return foundation::Result<void>::Success();
    }

    animator->crossfade = CrossfadeState{animator->playback.clip, clip, GameDuration{}, duration, 1.0f, 0.0f};
    animator->playback_state = AnimatorPlaybackState::Blending;
    animator->pose_state = options_.enable_mock_pose_evaluation ? PoseState::Evaluating : PoseState::Dirty;
    ++animator->revision;
    QueueEvent(handle.id, "animation.crossfade", static_cast<float>(duration.ticks));
    return foundation::Result<void>::Success();
}

std::size_t AnimationRuntime::Tick(std::size_t max_animators)
{
    return Tick(GameDuration{1}, max_animators);
}

std::size_t AnimationRuntime::Tick(GameDuration delta, std::size_t max_animators)
{
    const std::size_t limit = max_animators == 0 ? animators_.size() : max_animators;
    std::size_t transitioned = 0;
    const float delta_seconds = static_cast<float>(std::max<std::int64_t>(0, delta.ticks));

    for (const AnimatorInstanceId id : BuildAnimatorWorkList())
    {
        if (transitioned >= limit)
        {
            break;
        }

        AnimatorRecord& animator = animators_[id];
        if (animator.playback_state == AnimatorPlaybackState::Playing || animator.playback_state == AnimatorPlaybackState::Blending)
        {
            AdvancePlayback(animator, GameDuration{static_cast<std::int64_t>(delta_seconds)});
            if (animator.playback_state == AnimatorPlaybackState::Blending)
            {
                AdvanceCrossfade(animator, GameDuration{static_cast<std::int64_t>(delta_seconds)});
            }
            animator.pose_state = animator.desc.lod == AnimationLodLevel::Frozen ? PoseState::Clean : PoseState::Ready;
            const auto clip_result = ResolveClip(animator.playback.clip);
            if (clip_result && animator.playback_state != AnimatorPlaybackState::Blending && !animator.playback.loop &&
                static_cast<double>(animator.playback.local_time.ticks) >= static_cast<double>(clip_result.Value().duration_seconds))
            {
                animator.playback_state = AnimatorPlaybackState::Finished;
                QueueEvent(id, "animation.finished", static_cast<float>(animator.playback.local_time.ticks));
            }
            (void)PublishPose(animator);
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

    return ToLegacyState(*animator);
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
        return PoseSnapshot{id, {}, PoseState::Clean, AnimationLodLevel::Frozen, 0};
    }

    return PoseSnapshot{id, animator->handle, animator->pose_state, animator->desc.lod, animator->revision};
}

PoseBuffer AnimationRuntime::GetPoseBuffer(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return PoseBuffer{handle, {}, 0};
    }

    return BuildPoseBuffer(*animator);
}

std::span<const AnimationEvent> AnimationRuntime::Events() const
{
    return events_;
}

void AnimationRuntime::Clear()
{
    events_.clear();
}

foundation::Result<SkeletonDesc> AnimationRuntime::ResolveSkeleton(SkeletonId id)
{
    const auto iterator = skeletons_.find(id);
    if (iterator != skeletons_.end())
    {
        return foundation::Result<SkeletonDesc>::Success(iterator->second);
    }

    if (dependencies_.resources == nullptr)
    {
        return foundation::Result<SkeletonDesc>::Failure(
            foundation::Error::Create("animation.skeleton_not_found", "skeleton was not registered"));
    }

    const auto loaded = dependencies_.resources->LoadSkeleton(id);
    if (!loaded)
    {
        return loaded;
    }

    skeletons_[id] = loaded.Value();
    return foundation::Result<SkeletonDesc>::Success(loaded.Value());
}

foundation::Result<AnimationClipDesc> AnimationRuntime::ResolveClip(AnimationClipId id)
{
    const auto iterator = clips_.find(id);
    if (iterator != clips_.end())
    {
        return foundation::Result<AnimationClipDesc>::Success(iterator->second);
    }

    if (dependencies_.resources == nullptr)
    {
        return foundation::Result<AnimationClipDesc>::Failure(
            foundation::Error::Create("animation.clip_not_found", "animation clip was not registered"));
    }

    const auto loaded = dependencies_.resources->LoadClip(id);
    if (!loaded)
    {
        return loaded;
    }

    clips_[id] = loaded.Value();
    return foundation::Result<AnimationClipDesc>::Success(loaded.Value());
}

AnimatorState AnimationRuntime::ToLegacyState(const AnimatorRecord& animator) const noexcept
{
    if (animator.lifecycle == AnimatorLifecycle::Disabled)
    {
        return AnimatorState::Disabled;
    }
    if (animator.readiness == AnimatorReadiness::ResourceMissing)
    {
        return AnimatorState::ResourceMissing;
    }

    switch (animator.playback_state)
    {
    case AnimatorPlaybackState::Stopped:
        return AnimatorState::Ready;
    case AnimatorPlaybackState::Playing:
        return AnimatorState::Playing;
    case AnimatorPlaybackState::Paused:
        return AnimatorState::Paused;
    case AnimatorPlaybackState::Blending:
        return AnimatorState::Blending;
    case AnimatorPlaybackState::Finished:
        return AnimatorState::Finished;
    }

    return AnimatorState::Uninitialized;
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

AnimationRuntime::AnimatorRecord* AnimationRuntime::FindAnimator(AnimatorHandle handle)
{
    AnimatorRecord* animator = FindAnimator(handle.id);
    if (animator == nullptr || animator->handle.generation != handle.generation)
    {
        return nullptr;
    }

    return animator;
}

const AnimationRuntime::AnimatorRecord* AnimationRuntime::FindAnimator(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle.id);
    if (animator == nullptr || animator->handle.generation != handle.generation)
    {
        return nullptr;
    }

    return animator;
}

std::vector<AnimatorInstanceId> AnimationRuntime::BuildAnimatorWorkList() const
{
    std::vector<AnimatorInstanceId> work_list;
    work_list.reserve(animators_.size());
    for (const auto& [id, animator] : animators_)
    {
        if (animator.playback_state == AnimatorPlaybackState::Playing || animator.playback_state == AnimatorPlaybackState::Blending)
        {
            work_list.push_back(id);
        }
    }

    std::sort(work_list.begin(), work_list.end(), [](AnimatorInstanceId left, AnimatorInstanceId right) {
        return left.value < right.value;
    });
    return work_list;
}

PoseBuffer AnimationRuntime::BuildPoseBuffer(const AnimatorRecord& animator) const
{
    auto skeleton = skeletons_.find(animator.desc.skeleton);
    std::uint32_t bone_count = skeleton == skeletons_.end() ? 0 : skeleton->second.joint_count;
    if (bone_count == 0 && dependencies_.resources != nullptr)
    {
        const auto loaded = dependencies_.resources->LoadSkeleton(animator.desc.skeleton);
        if (loaded)
        {
            bone_count = loaded.Value().joint_count;
        }
    }

    PoseBuffer pose{animator.handle, std::vector<Transform>(bone_count), animator.revision};
    const float sample = static_cast<float>(animator.playback.local_time.ticks);
    for (std::size_t index = 0; index < pose.bone_transforms.size(); ++index)
    {
        pose.bone_transforms[index].position.x = sample;
        pose.bone_transforms[index].position.y = static_cast<float>(index);
        if (animator.crossfade)
        {
            pose.bone_transforms[index].position.z = animator.crossfade->target_weight;
        }
    }
    return pose;
}

foundation::Result<void> AnimationRuntime::PublishPose(const AnimatorRecord& animator)
{
    if (dependencies_.pose_sink == nullptr || !options_.enable_mock_pose_evaluation)
    {
        return foundation::Result<void>::Success();
    }

    return dependencies_.pose_sink->Publish(std::make_shared<const PoseBuffer>(BuildPoseBuffer(animator)));
}

void AnimationRuntime::AdvancePlayback(AnimatorRecord& animator, GameDuration delta)
{
    const double scaled = static_cast<double>(std::max<std::int64_t>(0, delta.ticks)) * animator.playback.playback_rate;
    animator.playback.local_time.ticks += static_cast<std::int64_t>(scaled);
}

void AnimationRuntime::AdvanceCrossfade(AnimatorRecord& animator, GameDuration delta)
{
    if (!animator.crossfade)
    {
        return;
    }

    animator.crossfade->elapsed.ticks += std::max<std::int64_t>(0, delta.ticks);
    const auto duration = std::max<std::int64_t>(1, animator.crossfade->duration.ticks);
    const float t = std::clamp(static_cast<float>(animator.crossfade->elapsed.ticks) / static_cast<float>(duration), 0.0f, 1.0f);
    animator.crossfade->source_weight = 1.0f - t;
    animator.crossfade->target_weight = t;
    if (t >= 1.0f)
    {
        animator.playback.clip = animator.crossfade->target_clip;
        animator.playback.local_time = GameDuration{};
        animator.playback_state = AnimatorPlaybackState::Playing;
        animator.crossfade.reset();
    }
}

void AnimationRuntime::QueueEvent(AnimatorInstanceId animator, std::string name, float time)
{
    events_.push_back(AnimationEvent{animator, std::move(name), time});
}
} // namespace epidemic::runtime::animation
