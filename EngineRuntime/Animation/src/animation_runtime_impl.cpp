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
    if (skeletons_.contains(desc.id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.duplicate_skeleton", "skeleton is already registered"));
    }

    skeletons_[desc.id] = desc;
    return foundation::Result<void>::Success();
}

bool AnimationRuntime::HasSkeleton(SkeletonId id) const
{
    if (skeletons_.contains(id))
    {
        return true;
    }
    if (dependencies_.resources == nullptr)
    {
        return false;
    }

    const auto loaded = dependencies_.resources->LoadSkeleton(id);
    return loaded && loaded.Value().id == id && loaded.Value().joint_count != 0;
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
    if (clips_.contains(desc.id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.duplicate_clip", "animation clip is already registered"));
    }

    clips_[desc.id] = desc;
    return foundation::Result<void>::Success();
}

bool AnimationRuntime::HasClip(AnimationClipId id) const
{
    if (clips_.contains(id))
    {
        return true;
    }
    if (dependencies_.resources == nullptr)
    {
        return false;
    }

    const auto loaded = dependencies_.resources->LoadClip(id);
    return loaded && loaded.Value().id == id && loaded.Value().skeleton.IsValid() && loaded.Value().duration_seconds > 0.0f;
}

foundation::Result<AnimatorHandle> AnimationRuntime::CreateAnimatorHandle(const AnimatorDesc& desc)
{
    if (!desc.owner.IsValid())
    {
        return foundation::Result<AnimatorHandle>::Failure(
            foundation::Error::Create("animation.invalid_owner", "animator owner must be valid before creation"));
    }

    const auto skeleton = ResolveSkeleton(desc.skeleton);
    if (!skeleton)
    {
        return foundation::Result<AnimatorHandle>::Failure(
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
    record.cached_pose = PoseBuffer{handle, std::vector<Transform>(skeleton.Value().joint_count), record.revision};
    animators_.emplace(id, record);
    return foundation::Result<AnimatorHandle>::Success(handle);
}

foundation::Result<void> AnimationRuntime::DestroyAnimator(AnimatorHandle handle)
{
    AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for destruction"));
    }

    animators_.erase(handle.id);
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

    if (command.playback_rate < 0.0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_playback", "animation playback rate must be non-negative"));
    }

    const auto clip_result = ResolveClip(command.clip);
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
    animator->playback = AnimatorPlayback{command.clip, command.loop, command.playback_rate, FrameDuration{}};
    animator->crossfade.reset();
    animator->playback_state = AnimatorPlaybackState::Playing;
    animator->pose_state = dependencies_.evaluator != nullptr ? PoseState::Evaluating : PoseState::Dirty;
    ++animator->revision;
    QueueEvent(command.animator.id, "animation.started", 0.0f);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::Pause(AnimatorHandle handle)
{
    AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pause"));
    }

    if (animator->playback_state != AnimatorPlaybackState::Playing && animator->playback_state != AnimatorPlaybackState::Blending)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_playback_transition", "only playing or blending animators can be paused"));
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
        animator->playback.local_time = FrameDuration{};
        animator->crossfade.reset();
        animator->pose_state = PoseState::Clean;
        ++animator->revision;
        QueueEvent(handle.id, "animation.stopped", 0.0f);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::Crossfade(AnimatorHandle handle, AnimationClipId clip, FrameDuration duration)
{
    if (duration.IsNegative())
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
        const auto play = Play(AnimationPlaybackCommand{handle, clip, false});
        if (!play)
        {
            return play;
        }
        return foundation::Result<void>::Success();
    }

    if (duration.value.count() == 0)
    {
        animator->playback = AnimatorPlayback{clip, false, 1.0, FrameDuration{}};
        animator->playback_state = AnimatorPlaybackState::Playing;
        animator->crossfade.reset();
        ++animator->revision;
        return foundation::Result<void>::Success();
    }

    animator->crossfade = CrossfadeState{animator->playback.clip, clip, animator->playback.local_time, FrameDuration{}, FrameDuration{}, duration, 1.0f, 0.0f};
    animator->playback_state = AnimatorPlaybackState::Blending;
    animator->pose_state = dependencies_.evaluator != nullptr ? PoseState::Evaluating : PoseState::Dirty;
    ++animator->revision;
    QueueEvent(handle.id, "animation.crossfade", static_cast<float>(duration.value.count()) / 1000000.0f);
    return foundation::Result<void>::Success();
}

foundation::Result<std::size_t> AnimationRuntime::Tick(FrameDuration delta, std::size_t max_animators)
{
    if (delta.IsNegative())
    {
        return foundation::Result<std::size_t>::Failure(
            foundation::Error::Create("animation.invalid_delta", "animation frame duration must not be negative"));
    }
    const std::size_t limit = max_animators == 0 ? animators_.size() : max_animators;
    std::size_t transitioned = 0;

    for (const AnimatorInstanceId id : BuildAnimatorWorkList())
    {
        if (transitioned >= limit)
        {
            break;
        }

        AnimatorRecord& animator = animators_[id];
        if (animator.playback_state == AnimatorPlaybackState::Playing || animator.playback_state == AnimatorPlaybackState::Blending)
        {
            AdvancePlayback(animator, delta);
            if (animator.playback_state == AnimatorPlaybackState::Blending)
            {
                AdvanceCrossfade(animator, delta);
            }
            const auto evaluated = EvaluatePose(animator);
            if (!evaluated)
            {
                return foundation::Result<std::size_t>::Failure(evaluated.GetError());
            }
            animator.cached_pose = evaluated.Value();
            animator.pose_state = animator.desc.lod == AnimationLodLevel::Frozen ? PoseState::Clean : PoseState::Ready;
            const auto clip_result = ResolveClip(animator.playback.clip);
            if (clip_result && animator.playback_state != AnimatorPlaybackState::Blending)
            {
                const auto clip_microseconds = std::chrono::microseconds{
                    static_cast<std::int64_t>(static_cast<double>(clip_result.Value().duration_seconds) * 1000000.0)};
                if (clip_microseconds.count() > 0 && animator.playback.local_time.value >= clip_microseconds)
                {
                    if (animator.playback.loop)
                    {
                        animator.playback.local_time.value %= clip_microseconds;
                        QueueEvent(id, "animation.looped", static_cast<float>(animator.playback.local_time.value.count()) / 1000000.0f);
                    }
                    else
                    {
                        animator.playback.local_time.value = clip_microseconds;
                        animator.playback_state = AnimatorPlaybackState::Finished;
                        QueueEvent(id, "animation.finished", static_cast<float>(animator.playback.local_time.value.count()) / 1000000.0f);
                    }
                }
            }
            ++animator.revision;
            animator.cached_pose.revision = animator.revision;
            const auto published = PublishPose(animator);
            if (!published)
            {
                return foundation::Result<std::size_t>::Failure(published.GetError());
            }
            ++transitioned;
        }
    }

    return foundation::Result<std::size_t>::Success(transitioned);
}

foundation::Result<AnimatorSnapshot> AnimationRuntime::GetAnimatorSnapshot(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<AnimatorSnapshot>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for snapshot"));
    }

    return foundation::Result<AnimatorSnapshot>::Success(ToSnapshot(*animator));
}

foundation::Result<void> AnimationRuntime::SetLod(AnimatorHandle handle, AnimationLodLevel lod)
{
    AnimatorRecord* animator = FindAnimator(handle);
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

foundation::Result<PoseState> AnimationRuntime::GetPoseState(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<PoseState>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pose state"));
    }

    return foundation::Result<PoseState>::Success(animator->pose_state);
}

foundation::Result<PoseSnapshot> AnimationRuntime::GetPoseSnapshot(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<PoseSnapshot>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pose snapshot"));
    }

    return foundation::Result<PoseSnapshot>::Success(PoseSnapshot{handle.id, animator->handle, animator->pose_state, animator->desc.lod, animator->revision});
}

foundation::Result<PoseBuffer> AnimationRuntime::GetPoseBuffer(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<PoseBuffer>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pose buffer"));
    }

    return foundation::Result<PoseBuffer>::Success(animator->cached_pose);
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
    if (loaded.Value().id != id || loaded.Value().joint_count == 0)
    {
        return foundation::Result<SkeletonDesc>::Failure(
            foundation::Error::Create("animation.invalid_skeleton", "resource source returned invalid skeleton descriptor"));
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
    if (loaded.Value().id != id || !loaded.Value().skeleton.IsValid() || loaded.Value().duration_seconds <= 0.0f)
    {
        return foundation::Result<AnimationClipDesc>::Failure(
            foundation::Error::Create("animation.invalid_clip", "resource source returned invalid animation clip descriptor"));
    }

    clips_[id] = loaded.Value();
    return foundation::Result<AnimationClipDesc>::Success(loaded.Value());
}

AnimatorSnapshot AnimationRuntime::ToSnapshot(const AnimatorRecord& animator) noexcept
{
    return AnimatorSnapshot{
        animator.handle,
        animator.lifecycle,
        animator.readiness,
        animator.playback_state,
        animator.desc.lod,
        animator.playback.clip,
        animator.playback.local_time,
        animator.revision};
}

AnimationRuntime::AnimatorRecord* AnimationRuntime::FindAnimator(AnimatorHandle handle)
{
    const auto iterator = animators_.find(handle.id);
    if (iterator == animators_.end() || iterator->second.handle.generation != handle.generation)
    {
        return nullptr;
    }

    return &iterator->second;
}

const AnimationRuntime::AnimatorRecord* AnimationRuntime::FindAnimator(AnimatorHandle handle) const
{
    const auto iterator = animators_.find(handle.id);
    if (iterator == animators_.end() || iterator->second.handle.generation != handle.generation)
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
    const float sample = static_cast<float>(animator.playback.local_time.value.count()) / 1000000.0f;
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

foundation::Result<PoseBuffer> AnimationRuntime::EvaluatePose(const AnimatorRecord& animator)
{
    const auto skeleton = ResolveSkeleton(animator.desc.skeleton);
    if (!skeleton)
    {
        return foundation::Result<PoseBuffer>::Failure(skeleton.GetError());
    }
    const auto source_clip = ResolveClip(animator.crossfade ? animator.crossfade->source_clip : animator.playback.clip);
    if (!source_clip)
    {
        return foundation::Result<PoseBuffer>::Failure(source_clip.GetError());
    }
    const auto target_clip = ResolveClip(animator.crossfade ? animator.crossfade->target_clip : animator.playback.clip);
    if (!target_clip)
    {
        return foundation::Result<PoseBuffer>::Failure(target_clip.GetError());
    }

    AnimationEvaluationRequest request{};
    request.animator = animator.handle;
    request.skeleton = skeleton.Value();
    request.source_clip = source_clip.Value();
    request.target_clip = target_clip.Value();
    request.source_time = animator.crossfade ? animator.crossfade->source_time : animator.playback.local_time;
    request.target_time = animator.crossfade ? animator.crossfade->target_time : animator.playback.local_time;
    request.source_weight = animator.crossfade ? animator.crossfade->source_weight : 1.0f;
    request.target_weight = animator.crossfade ? animator.crossfade->target_weight : 0.0f;
    request.lod = animator.desc.lod;
    request.revision = animator.revision;

    if (dependencies_.evaluator == nullptr && !options_.enable_mock_pose_evaluation)
    {
        return foundation::Result<PoseBuffer>::Failure(
            foundation::Error::Create("animation.evaluator_missing", "animation evaluator backend is required for pose publication"));
    }

    if (dependencies_.evaluator != nullptr)
    {
        return dependencies_.evaluator->EvaluatePose(request);
    }

    return foundation::Result<PoseBuffer>::Success(BuildPoseBuffer(animator));
}

foundation::Result<void> AnimationRuntime::PublishPose(const AnimatorRecord& animator)
{
    if (dependencies_.pose_sink == nullptr || animator.desc.lod == AnimationLodLevel::Frozen)
    {
        return foundation::Result<void>::Success();
    }

    return dependencies_.pose_sink->Publish(std::make_shared<const PoseBuffer>(animator.cached_pose));
}

void AnimationRuntime::AdvancePlayback(AnimatorRecord& animator, FrameDuration delta)
{
    const double scaled = static_cast<double>(std::max<std::int64_t>(0, delta.value.count())) * animator.playback.playback_rate +
                          animator.playback.fractional_microseconds;
    const auto whole = static_cast<std::int64_t>(scaled);
    animator.playback.fractional_microseconds = scaled - static_cast<double>(whole);
    animator.playback.local_time.value += std::chrono::microseconds{whole};
}

void AnimationRuntime::AdvanceCrossfade(AnimatorRecord& animator, FrameDuration delta)
{
    if (!animator.crossfade)
    {
        return;
    }

    animator.crossfade->source_time = animator.playback.local_time;
    animator.crossfade->target_time.value += std::chrono::microseconds{std::max<std::int64_t>(0, delta.value.count())};
    animator.crossfade->elapsed.value += std::chrono::microseconds{std::max<std::int64_t>(0, delta.value.count())};
    const auto duration = std::max<std::int64_t>(1, animator.crossfade->duration.value.count());
    const float t = std::clamp(static_cast<float>(animator.crossfade->elapsed.value.count()) / static_cast<float>(duration), 0.0f, 1.0f);
    animator.crossfade->source_weight = 1.0f - t;
    animator.crossfade->target_weight = t;
    if (t >= 1.0f)
    {
        animator.playback.clip = animator.crossfade->target_clip;
        animator.playback.local_time = animator.crossfade->target_time;
        animator.playback_state = AnimatorPlaybackState::Playing;
        animator.crossfade.reset();
    }
}

void AnimationRuntime::QueueEvent(AnimatorInstanceId animator, std::string name, float time)
{
    if (options_.event_capacity != 0 && events_.size() >= options_.event_capacity)
    {
        events_.erase(events_.begin());
    }
    events_.push_back(AnimationEvent{animator, std::move(name), time});
}
} // namespace epidemic::runtime::animation

