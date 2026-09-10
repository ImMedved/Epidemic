#include "animation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"
#include "Epidemic/Runtime/Foundation/numeric_validation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <limits>
#include <exception>
#include <new>
#include <string_view>
#include <utility>

namespace epidemic::runtime::animation
{
AnimationRuntime::AnimationRuntime(AnimationOptions options, AnimationDependencies dependencies)
    : options_(options), dependencies_(std::move(dependencies))
{
    const std::size_t capacity = options_.event_capacity == 0 ? AnimationOptions{}.event_capacity : options_.event_capacity;
    try { events_.reserve(capacity); } catch (...) {}
}

foundation::Result<void> AnimationRuntime::RegisterSkeleton(SkeletonDesc desc)
{
    if (registries_frozen_) return foundation::Result<void>::Failure(foundation::Error::Create("animation.registry_frozen", "animation registries are frozen"));
    const auto validation = ValidateSkeleton(desc);
    if (!validation) return validation;
    if (skeletons_.contains(desc.id)) return foundation::Result<void>::Failure(foundation::Error::Create("animation.duplicate_skeleton", "skeleton is already registered"));
    try { skeletons_.emplace(desc.id, desc); }
    catch (...) { return foundation::Result<void>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to register skeleton")); }
    return foundation::Result<void>::Success();
}


bool AnimationRuntime::HasSkeleton(SkeletonId id) const
{
    if (skeletons_.contains(id)) return true;
    if (dependencies_.resources == nullptr) return false;
    try
    {
        const auto loaded = dependencies_.resources->LoadSkeleton(id);
        return loaded && loaded.Value().id == id && loaded.Value().joint_count != 0 &&
               (options_.max_skeleton_joints == 0 || loaded.Value().joint_count <= options_.max_skeleton_joints);
    }
    catch (...) { return false; }
}


foundation::Result<void> AnimationRuntime::RegisterClip(AnimationClipDesc desc)
{
    if (registries_frozen_) return foundation::Result<void>::Failure(foundation::Error::Create("animation.registry_frozen", "animation registries are frozen"));
    const auto validation = ValidateClip(desc);
    if (!validation) return validation;
    if (!HasSkeleton(desc.skeleton)) return foundation::Result<void>::Failure(foundation::Error::Create("animation.skeleton_not_found", "clip must reference a registered skeleton"));
    if (clips_.contains(desc.id)) return foundation::Result<void>::Failure(foundation::Error::Create("animation.duplicate_clip", "animation clip is already registered"));
    try { clips_.emplace(desc.id, desc); }
    catch (...) { return foundation::Result<void>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to register clip")); }
    return foundation::Result<void>::Success();
}


bool AnimationRuntime::HasClip(AnimationClipId id) const
{
    if (clips_.contains(id)) return true;
    if (dependencies_.resources == nullptr) return false;
    try
    {
        const auto loaded = dependencies_.resources->LoadClip(id);
        return loaded && loaded.Value().id == id && loaded.Value().skeleton.IsValid() && IsFinitePositive(loaded.Value().duration_seconds) &&
               CheckedSecondsToMicroseconds(static_cast<double>(loaded.Value().duration_seconds)).has_value();
    }
    catch (...) { return false; }
}

foundation::Result<void> AnimationRuntime::Freeze()
{
    registries_frozen_ = true;
    return foundation::Result<void>::Success();
}

bool AnimationRuntime::IsFrozen() const noexcept
{
    return registries_frozen_;
}


foundation::Result<AnimatorHandle> AnimationRuntime::CreateAnimatorHandle(const AnimatorDesc& desc)
{
    if (!desc.owner.IsValid()) return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.invalid_owner", "animator owner must be valid before creation"));
    switch (desc.lod) { case AnimationLodLevel::Full: case AnimationLodLevel::Reduced: case AnimationLodLevel::Frozen: break; default: return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.invalid_lod", "animator LOD value is invalid")); }
    if (options_.max_animators != 0 && animators_.size() >= options_.max_animators) return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.animator_capacity", "animator capacity is exhausted"));
    const auto skeleton = ResolveSkeleton(desc.skeleton);
    if (!skeleton) return foundation::Result<AnimatorHandle>::Failure(skeleton.GetError());
    const auto id_value = PeekMonotonicId(next_animator_value_, "animation.animator_id_exhausted", "animator id allocator is exhausted");
    const auto generation = PeekMonotonicId(next_generation_, "animation.animator_generation_exhausted", "animator generation allocator is exhausted");
    if (!id_value) return foundation::Result<AnimatorHandle>::Failure(id_value.GetError());
    if (!generation) return foundation::Result<AnimatorHandle>::Failure(generation.GetError());
    const AnimatorInstanceId id{id_value.Value()};
    const AnimatorHandle handle{id, generation.Value()};
    if (animators_.contains(id)) return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.duplicate_animator_id", "allocated animator id already exists"));
    try
    {
        AnimatorRecord record{}; record.desc=desc; record.handle=handle; record.lifecycle=AnimatorLifecycle::Ready; record.readiness=AnimatorReadiness::Ready;
        record.playback_state=AnimatorPlaybackState::Stopped; record.pose_state=PoseState::Clean; record.revision=1;
        record.cached_pose=PoseBuffer{handle, desc.owner, std::vector<Transform>(skeleton.Value().joint_count), record.revision};
        const auto [_, inserted]=animators_.emplace(id, std::move(record));
        if (!inserted) return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.duplicate_animator_id", "allocated animator id already exists"));
    }
    catch (...) { return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to allocate animator pose storage")); }
    CommitMonotonicId(next_animator_value_, id_value.Value()); CommitMonotonicId(next_generation_, generation.Value());
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
    AnimatorRecord* animator=FindAnimator(command.animator);
    if (!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found", "animator handle was not found for playback"));
    if (!IsFiniteNonNegative(command.playback_rate) || !CheckedScaleDuration(FrameDuration{std::chrono::microseconds{1}}, command.playback_rate)) return foundation::Result<void>::Failure(foundation::Error::Create("animation.invalid_playback", "animation playback rate exceeds supported range"));
    const auto revision=NextRevision(*animator); if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    const auto clip_result=ResolveClip(command.clip); if (!clip_result) return foundation::Result<void>::Failure(clip_result.GetError());
    if (clip_result.Value().skeleton != animator->desc.skeleton) return foundation::Result<void>::Failure(foundation::Error::Create("animation.skeleton_mismatch", "clip skeleton does not match animator skeleton"));
    animator->readiness=AnimatorReadiness::Ready; animator->playback=AnimatorPlayback{command.clip,command.loop,command.playback_rate,FrameDuration{},0.0}; animator->crossfade.reset();
    animator->playback_state=AnimatorPlaybackState::Playing; animator->pose_state=dependencies_.evaluator?PoseState::Evaluating:PoseState::Dirty; animator->revision=revision.Value();
    QueueEvent(command.animator.id,"animation.started",0.0f); return foundation::Result<void>::Success();
}


foundation::Result<void> AnimationRuntime::Pause(AnimatorHandle handle)
{
    AnimatorRecord* animator=FindAnimator(handle); if (!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pause"));
    if (animator->playback_state!=AnimatorPlaybackState::Playing && animator->playback_state!=AnimatorPlaybackState::Blending) return foundation::Result<void>::Failure(foundation::Error::Create("animation.invalid_playback_transition", "only playing or blending animators can be paused"));
    const auto revision=NextRevision(*animator); if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    animator->playback_state=AnimatorPlaybackState::Paused; animator->revision=revision.Value(); return foundation::Result<void>::Success();
}


foundation::Result<void> AnimationRuntime::Stop(AnimatorHandle handle)
{
    AnimatorRecord* animator=FindAnimator(handle); if (!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found", "animator handle was not found for stop"));
    if (animator->playback_state==AnimatorPlaybackState::Stopped) return foundation::Result<void>::Success();
    const auto revision=NextRevision(*animator); if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    animator->playback_state=AnimatorPlaybackState::Stopped; animator->playback.local_time=FrameDuration{}; animator->playback.fractional_microseconds=0.0; animator->crossfade.reset(); animator->pose_state=PoseState::Clean; animator->revision=revision.Value();
    QueueEvent(handle.id,"animation.stopped",0.0f); return foundation::Result<void>::Success();
}


foundation::Result<void> AnimationRuntime::Crossfade(AnimatorHandle handle, AnimationClipId clip, FrameDuration duration)
{
    if (duration.IsNegative()) return foundation::Result<void>::Failure(foundation::Error::Create("animation.invalid_fade", "crossfade duration must not be negative"));
    AnimatorRecord* animator=FindAnimator(handle); if (!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found", "animator handle was not found for crossfade"));
    const auto revision=NextRevision(*animator); if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    const auto clip_result=ResolveClip(clip); if (!clip_result) return foundation::Result<void>::Failure(clip_result.GetError());
    if (clip_result.Value().skeleton!=animator->desc.skeleton) return foundation::Result<void>::Failure(foundation::Error::Create("animation.skeleton_mismatch", "clip skeleton does not match animator skeleton"));
    if (!animator->playback.clip.IsValid()) return Play(AnimationPlaybackCommand{handle,clip,false,1.0});
    if (duration.IsZero()) { animator->playback=AnimatorPlayback{clip,false,1.0,FrameDuration{},0.0}; animator->playback_state=AnimatorPlaybackState::Playing; animator->crossfade.reset(); animator->pose_state=dependencies_.evaluator?PoseState::Evaluating:PoseState::Dirty; animator->revision=revision.Value(); return foundation::Result<void>::Success(); }
    animator->crossfade=CrossfadeState{animator->playback.clip,clip,animator->playback.local_time,FrameDuration{},FrameDuration{},duration,1.0f,0.0f}; animator->playback_state=AnimatorPlaybackState::Blending; animator->pose_state=dependencies_.evaluator?PoseState::Evaluating:PoseState::Dirty; animator->revision=revision.Value();
    QueueEvent(handle.id,"animation.crossfade",static_cast<float>(duration.value.count())/1000000.0f); return foundation::Result<void>::Success();
}


foundation::Result<std::size_t> AnimationRuntime::Tick(FrameDuration delta, std::size_t max_animators)
{
    if (delta.IsNegative()) return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.invalid_delta", "animation frame duration must not be negative"));
    std::vector<AnimatorInstanceId> work_list; try { work_list=BuildAnimatorWorkList(); } catch (...) { return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to build animator tick work list")); }
    const std::size_t limit=max_animators==0?animators_.size():max_animators; std::size_t transitioned=0;
    for (auto id:work_list)
    {
        if (transitioned >= limit)
        {
            break;
        }
        AnimatorRecord& live = animators_.at(id);
        AnimatorRecord staged; try { staged=live; } catch (...) { return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to stage animator state")); }
        const auto revision=NextRevision(staged); if (!revision) return foundation::Result<std::size_t>::Failure(revision.GetError());
        const auto a=AdvancePlayback(staged,delta); if (!a) return foundation::Result<std::size_t>::Failure(a.GetError());
        if (staged.playback_state==AnimatorPlaybackState::Blending) { const auto b=AdvanceCrossfade(staged,delta); if (!b) return foundation::Result<std::size_t>::Failure(b.GetError()); }
        foundation::Result<PoseBuffer> evaluated=foundation::Result<PoseBuffer>::Failure(foundation::Error::Create("animation.evaluation_failed","evaluation unavailable"));
        try { evaluated=EvaluatePose(staged); } catch (...) { return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.evaluator_exception", "animation evaluator callback threw")); }
        if (!evaluated) return foundation::Result<std::size_t>::Failure(evaluated.GetError());
        staged.cached_pose=std::move(evaluated.Value()); staged.pose_state=staged.desc.lod==AnimationLodLevel::Frozen?PoseState::Clean:PoseState::Ready;
        bool looped=false, finished=false; const auto clip=ResolveClip(staged.playback.clip); if (!clip) return foundation::Result<std::size_t>::Failure(clip.GetError());
        const auto duration=CheckedSecondsToMicroseconds(static_cast<double>(clip.Value().duration_seconds)); if (!duration) return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.clip_duration_overflow", "animation clip duration exceeds runtime time range"));
        if (staged.playback_state!=AnimatorPlaybackState::Blending && !duration->IsZero() && staged.playback.local_time.value>=duration->value) { if (staged.playback.loop) { staged.playback.local_time.value%=duration->value; looped=true; } else { staged.playback.local_time=*duration; staged.playback_state=AnimatorPlaybackState::Finished; finished=true; } }
        staged.revision=revision.Value(); staged.cached_pose.revision=staged.revision; live=std::move(staged);
        if (looped)
        {
            QueueEvent(id, "animation.looped", static_cast<float>(live.playback.local_time.value.count()) / 1000000.0f);
        }
        if (finished)
        {
            QueueEvent(id, "animation.finished", static_cast<float>(live.playback.local_time.value.count()) / 1000000.0f);
        }
        const auto published=PublishPose(live); if(!published) return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.pose_publication_failed_after_commit", "pose publication failed after animator frame committed")); ++transitioned;
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
    switch(lod){case AnimationLodLevel::Full:case AnimationLodLevel::Reduced:case AnimationLodLevel::Frozen:break;default:return foundation::Result<void>::Failure(foundation::Error::Create("animation.invalid_lod","animation LOD value is invalid"));}
    AnimatorRecord* animator=FindAnimator(handle); if(!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found","animator was not found for LOD update"));
    const auto revision=NextRevision(*animator); if(!revision) return foundation::Result<void>::Failure(revision.GetError()); animator->desc.lod=lod; animator->pose_state=lod==AnimationLodLevel::Frozen?PoseState::Clean:PoseState::Dirty; animator->revision=revision.Value(); return foundation::Result<void>::Success();
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
    const auto it=skeletons_.find(id); if(it!=skeletons_.end()) return foundation::Result<SkeletonDesc>::Success(it->second);
    if(!dependencies_.resources) return foundation::Result<SkeletonDesc>::Failure(foundation::Error::Create("animation.skeleton_not_found","skeleton was not registered"));
    foundation::Result<SkeletonDesc> loaded=foundation::Result<SkeletonDesc>::Failure(foundation::Error::Create("animation.resource_exception","skeleton callback did not run")); try{loaded=dependencies_.resources->LoadSkeleton(id);}catch(...){return foundation::Result<SkeletonDesc>::Failure(foundation::Error::Create("animation.resource_exception","skeleton resource callback threw"));}
    if (!loaded)
    {
        return loaded;
    }
    const auto v = ValidateSkeleton(loaded.Value());
    if (!v || loaded.Value().id != id)
    {
        return foundation::Result<SkeletonDesc>::Failure(
            foundation::Error::Create("animation.invalid_skeleton", "resource source returned invalid skeleton descriptor"));
    }
    if(!registries_frozen_) { try{skeletons_.emplace(id,loaded.Value());}catch(...){return foundation::Result<SkeletonDesc>::Failure(foundation::Error::Create("animation.allocation_failed","failed to cache skeleton"));} } return foundation::Result<SkeletonDesc>::Success(loaded.Value());
}


foundation::Result<AnimationClipDesc> AnimationRuntime::ResolveClip(AnimationClipId id)
{
    const auto it=clips_.find(id); if(it!=clips_.end()) return foundation::Result<AnimationClipDesc>::Success(it->second);
    if(!dependencies_.resources) return foundation::Result<AnimationClipDesc>::Failure(foundation::Error::Create("animation.clip_not_found","animation clip was not registered"));
    foundation::Result<AnimationClipDesc> loaded=foundation::Result<AnimationClipDesc>::Failure(foundation::Error::Create("animation.resource_exception","clip callback did not run")); try{loaded=dependencies_.resources->LoadClip(id);}catch(...){return foundation::Result<AnimationClipDesc>::Failure(foundation::Error::Create("animation.resource_exception","clip resource callback threw"));}
    if (!loaded)
    {
        return loaded;
    }
    const auto v = ValidateClip(loaded.Value());
    if (!v || loaded.Value().id != id)
    {
        return foundation::Result<AnimationClipDesc>::Failure(
            foundation::Error::Create("animation.invalid_clip", "resource source returned invalid clip descriptor"));
    }
    if(!registries_frozen_) { try{clips_.emplace(id,loaded.Value());}catch(...){return foundation::Result<AnimationClipDesc>::Failure(foundation::Error::Create("animation.allocation_failed","failed to cache clip"));} } return foundation::Result<AnimationClipDesc>::Success(loaded.Value());
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

    PoseBuffer pose{animator.handle, animator.desc.owner, std::vector<Transform>(bone_count), animator.revision};
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
    request.owner = animator.desc.owner;
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
    if(!dependencies_.pose_sink || animator.desc.lod==AnimationLodLevel::Frozen) return foundation::Result<void>::Success();
    try{return dependencies_.pose_sink->Publish(std::make_shared<const PoseBuffer>(animator.cached_pose));}catch(...){return foundation::Result<void>::Failure(foundation::Error::Create("animation.publish_exception","animation pose sink callback threw"));}
}


foundation::Result<void> AnimationRuntime::AdvancePlayback(AnimatorRecord& animator, FrameDuration delta)
{
    const long double scaled=static_cast<long double>(delta.value.count())*static_cast<long double>(animator.playback.playback_rate)+static_cast<long double>(animator.playback.fractional_microseconds);
    if(!std::isfinite(static_cast<double>(scaled))||scaled<0.0L||scaled>static_cast<long double>(std::numeric_limits<std::int64_t>::max())) return foundation::Result<void>::Failure(foundation::Error::Create("animation.time_overflow","scaled playback delta exceeds runtime range"));
    const auto whole=static_cast<std::int64_t>(scaled); const auto next=CheckedAdd(animator.playback.local_time,FrameDuration{std::chrono::microseconds{whole}}); if(!next) return foundation::Result<void>::Failure(foundation::Error::Create("animation.time_overflow","animation local time overflow"));
    animator.playback.fractional_microseconds=static_cast<double>(scaled-static_cast<long double>(whole)); animator.playback.local_time=*next; return foundation::Result<void>::Success();
}


foundation::Result<void> AnimationRuntime::AdvanceCrossfade(AnimatorRecord& animator, FrameDuration delta)
{
    if (!animator.crossfade)
    {
        return foundation::Result<void>::Success();
    }
    const auto target = CheckedAdd(animator.crossfade->target_time, delta);
    const auto elapsed = CheckedAdd(animator.crossfade->elapsed, delta);
    if (!target || !elapsed)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.time_overflow", "animation crossfade time overflow"));
    }
    animator.crossfade->source_time=animator.playback.local_time; animator.crossfade->target_time=*target; animator.crossfade->elapsed=*elapsed; const auto duration=std::max<std::int64_t>(1,animator.crossfade->duration.value.count()); const float t=std::clamp(static_cast<float>(animator.crossfade->elapsed.value.count())/static_cast<float>(duration),0.0f,1.0f); animator.crossfade->source_weight=1.0f-t; animator.crossfade->target_weight=t; if(t>=1.0f){animator.playback.clip=animator.crossfade->target_clip;animator.playback.local_time=animator.crossfade->target_time;animator.playback_state=AnimatorPlaybackState::Playing;animator.crossfade.reset();} return foundation::Result<void>::Success();
}


void AnimationRuntime::QueueEvent(AnimatorInstanceId animator, std::string_view name, float time) noexcept
{
    try { const std::size_t cap=options_.event_capacity==0?AnimationOptions{}.event_capacity:options_.event_capacity; if(cap!=0&&events_.size()>=cap) events_.erase(events_.begin()); events_.push_back(AnimationEvent{animator,std::string{name},time}); } catch (...) {}
}

foundation::Result<std::uint64_t> AnimationRuntime::NextRevision(const AnimatorRecord& animator) const
{
    const auto next=CheckedRevisionIncrement(animator.revision); if(!next) return foundation::Result<std::uint64_t>::Failure(foundation::Error::Create("animation.revision_exhausted","animator revision is exhausted")); return foundation::Result<std::uint64_t>::Success(*next);
}

foundation::Result<void> AnimationRuntime::ValidateSkeleton(const SkeletonDesc& desc) const
{
    if (!desc.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_skeleton", "skeleton id must be valid"));
    }
    if (desc.joint_count == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.empty_skeleton", "skeleton must contain at least one joint"));
    }
    if (options_.max_skeleton_joints != 0 && desc.joint_count > options_.max_skeleton_joints)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.skeleton_too_large", "skeleton joint count exceeds runtime limit"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::ValidateClip(const AnimationClipDesc& desc) const
{
    if (!desc.id.IsValid() || !desc.skeleton.IsValid() || !IsFinitePositive(desc.duration_seconds))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_clip", "animation clip descriptor is invalid"));
    }
    if (!CheckedSecondsToMicroseconds(static_cast<double>(desc.duration_seconds)))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.clip_duration_overflow", "animation clip duration exceeds runtime time range"));
    }
    return foundation::Result<void>::Success();
}

void AnimationRuntime::SetRevisionForTesting(AnimatorHandle handle, std::uint64_t revision) noexcept { if(auto* a=FindAnimator(handle)) a->revision=revision; }
void AnimationRuntime::SetNextIdentityForTesting(std::uint64_t id, std::uint32_t generation) noexcept { next_animator_value_=id; next_generation_=generation; }

} // namespace epidemic::runtime::animation

