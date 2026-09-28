#include "animation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <cfloat>
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>
#include <vector>

using epidemic::runtime::animation::FrameDuration;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::animation::AnimationClipDesc;
using epidemic::runtime::animation::AnimationClipId;
using epidemic::runtime::animation::AnimationEvaluationRequest;
using epidemic::runtime::animation::AnimationLodLevel;
using epidemic::runtime::animation::AnimationOptions;
using epidemic::runtime::animation::AnimationPlaybackCommand;
using epidemic::runtime::animation::AnimationDependencies;
using epidemic::runtime::animation::CreateAnimationServices;
using epidemic::runtime::animation::CreateMockAnimationServices;
using epidemic::runtime::animation::CreateReferenceAnimationServices;
using epidemic::runtime::animation::IAnimationPoseSink;
using epidemic::runtime::animation::IAnimationEvaluatorBackend;
using epidemic::runtime::animation::IAnimationResourceSource;
using epidemic::runtime::animation::AnimationRuntime;
using epidemic::runtime::animation::AnimatorDesc;
using epidemic::runtime::animation::AnimatorPlaybackState;
using epidemic::runtime::animation::AnimatorReadiness;
using epidemic::runtime::animation::PoseBuffer;
using epidemic::runtime::animation::PoseState;
using epidemic::runtime::animation::SkeletonDesc;
using epidemic::runtime::animation::SkeletonId;

namespace
{
struct TestResourceSource final : IAnimationResourceSource
{
    bool fail_clip = false;
    bool fail_skeleton = false;
    float clip_duration = 2.0f;
    std::uint32_t skeleton_joint_count = 16;
    bool throw_clip = false;
    bool throw_skeleton = false;

    epidemic::foundation::Result<SkeletonDesc> LoadSkeleton(SkeletonId id) const override
    {
        if (throw_skeleton)
        {
            throw std::runtime_error("skeleton load throw");
        }
        if (fail_skeleton)
        {
            return epidemic::foundation::Result<SkeletonDesc>::Failure(
                epidemic::foundation::Error::Create("animation.skeleton_not_found", "test skeleton missing"));
        }
        return epidemic::foundation::Result<SkeletonDesc>::Success(SkeletonDesc{id, skeleton_joint_count});
    }

    epidemic::foundation::Result<AnimationClipDesc> LoadClip(AnimationClipId id) const override
    {
        if (throw_clip)
        {
            throw std::runtime_error("clip load throw");
        }
        if (fail_clip)
        {
            return epidemic::foundation::Result<AnimationClipDesc>::Failure(
                epidemic::foundation::Error::Create("animation.clip_not_found", "test clip missing"));
        }
        return epidemic::foundation::Result<AnimationClipDesc>::Success(AnimationClipDesc{id, SkeletonId{1}, clip_duration});
    }
};

struct TestPoseSink final : IAnimationPoseSink
{
    std::vector<std::shared_ptr<const PoseBuffer>> published;
    bool fail = false;
    bool throw_on_publish = false;

    epidemic::foundation::Result<void> Publish(std::shared_ptr<const PoseBuffer> pose) override
    {
        if (throw_on_publish)
        {
            throw std::runtime_error("pose sink throw");
        }
        if (fail)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("animation.publish_failed", "pose publish failed for test"));
        }
        published.push_back(std::move(pose));
        return epidemic::foundation::Result<void>::Success();
    }
};

struct TestEvaluatorBackend final : IAnimationEvaluatorBackend
{
    bool fail = false;
    bool throw_on_evaluate = false;
    bool wrong_owner = false;

    epidemic::foundation::Result<PoseBuffer> EvaluatePose(const AnimationEvaluationRequest& request) const override
    {
        ++evaluations;
        if (throw_on_evaluate)
        {
            throw std::runtime_error("evaluator throw");
        }
        if (fail)
        {
            return epidemic::foundation::Result<PoseBuffer>::Failure(
                epidemic::foundation::Error::Create("animation.evaluate_failed", "test evaluator failure"));
        }
        PoseBuffer pose{request.animator, wrong_owner ? RuntimeObjectId{request.owner.value + 1} : request.owner, std::vector<epidemic::runtime::Transform>(request.skeleton.joint_count), request.revision};
        for (std::size_t index = 0; index < pose.bone_transforms.size(); ++index)
        {
            pose.bone_transforms[index].position.x = static_cast<float>(request.target_time.value.count()) / 1000000.0f;
            pose.bone_transforms[index].position.y = static_cast<float>(index);
            pose.bone_transforms[index].position.z = request.target_weight;
        }
        return epidemic::foundation::Result<PoseBuffer>::Success(std::move(pose));
    }

    mutable int evaluations = 0;
};

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
}

template <typename TRuntime>
bool TickValue(TRuntime& runtime, FrameDuration delta, std::size_t max_animators, std::size_t expected)
{
    const auto tick = runtime.Tick(delta, max_animators);
    return tick.HasValue() && tick.Value() == expected;
}

template <typename TRuntime>
bool TickAtLeast(TRuntime& runtime, FrameDuration delta, std::size_t max_animators, std::size_t minimum)
{
    const auto tick = runtime.Tick(delta, max_animators);
    return tick.HasValue() && tick.Value() >= minimum;
}

template <typename TRuntime>
bool ExpectPlayback(const TRuntime& runtime, epidemic::runtime::animation::AnimatorHandle handle, AnimatorPlaybackState expected, std::string_view message)
{
    const auto snapshot = runtime.GetAnimatorSnapshot(handle);
    return Expect(snapshot.HasValue() && snapshot.Value().playback == expected, message);
}

template <typename TRuntime>
bool ExpectReadiness(const TRuntime& runtime, epidemic::runtime::animation::AnimatorHandle handle, AnimatorReadiness expected, std::string_view message)
{
    const auto snapshot = runtime.GetAnimatorSnapshot(handle);
    return Expect(snapshot.HasValue() && snapshot.Value().readiness == expected, message);
}

bool SeedResources(AnimationRuntime& runtime)
{
    bool ok = runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1}, 32}).HasValue();
    ok &= runtime.RegisterClip(AnimationClipDesc{AnimationClipId{10}, SkeletonId{1}, 1.0f}).HasValue();
    ok &= runtime.RegisterClip(AnimationClipDesc{AnimationClipId{11}, SkeletonId{1}, 4.0f}).HasValue();
    return ok;
}

bool TestCreateDestroyAnimator()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto created = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(created.HasValue(), "valid animator should be created");
    ok &= ExpectPlayback(runtime, created.Value(), AnimatorPlaybackState::Stopped, "new animator should be stopped and ready");
    ok &= Expect(runtime.DestroyAnimator(created.Value()).HasValue(), "created animator should destroy");
    ok &= Expect(!runtime.GetAnimatorSnapshot(created.Value()).HasValue(), "destroyed animator snapshot should fail");
    return ok;
}

bool TestPlayInvalidClipReturnsError()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto created = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(created.HasValue(), "valid animator should be created");
    ok &= Expect(!runtime.Play(AnimationPlaybackCommand{created.Value(), AnimationClipId{999}}).HasValue(), "missing clip should fail playback");
    ok &= ExpectReadiness(runtime, created.Value(), AnimatorReadiness::Ready, "failed play should leave readiness unchanged");
    return ok;
}

bool TestStateTransitionsAndEvents()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto created = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(created.HasValue(), "valid animator should be created");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{created.Value(), AnimationClipId{10}}).HasValue(), "registered clip should play");
    ok &= ExpectPlayback(runtime, created.Value(), AnimatorPlaybackState::Playing, "animator should be playing");
    const auto play_pose = runtime.GetPoseSnapshot(created.Value());
    ok &= Expect(play_pose.HasValue() && play_pose.Value().revision == 2, "play should revise pose snapshot");
    ok &= Expect(runtime.Events().size() == 1, "play should queue started event");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{1000000}}, 1, 1), "tick should advance playing animator");
    ok &= ExpectPlayback(runtime, created.Value(), AnimatorPlaybackState::Finished, "non-looping mock clip should finish after tick");
    const auto pose_state = runtime.GetPoseState(created.Value());
    const auto tick_pose = runtime.GetPoseSnapshot(created.Value());
    ok &= Expect(pose_state.HasValue() && pose_state.Value() == PoseState::Ready, "pose should become ready after mock evaluation");
    ok &= Expect(tick_pose.HasValue() && tick_pose.Value().revision == 3, "tick should revise pose snapshot");
    ok &= Expect(runtime.Events().size() == 2, "finish should queue second event");
    runtime.Clear();
    ok &= Expect(runtime.Events().empty(), "event buffer should clear");
    return ok;
}

bool TestTickOrderIsDeterministic()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto first = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    const auto second = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{78}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(first.HasValue() && second.HasValue(), "animators should be created");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{first.Value(), AnimationClipId{10}}).HasValue(), "first should play");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{second.Value(), AnimationClipId{10}}).HasValue(), "second should play");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{1000000}}, 1, 1), "budget should advance one animator");
    ok &= ExpectPlayback(runtime, first.Value(), AnimatorPlaybackState::Finished, "first id should advance first");
    ok &= ExpectPlayback(runtime, second.Value(), AnimatorPlaybackState::Playing, "second id should wait");
    return ok;
}

bool TestLodPlaceholderChangesPoseState()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto created = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(created.HasValue(), "valid animator should be created");
    ok &= Expect(runtime.SetLod(created.Value(), AnimationLodLevel::Frozen).HasValue(), "lod update should succeed");
    const auto pose_state = runtime.GetPoseState(created.Value());
    ok &= Expect(pose_state.HasValue() && pose_state.Value() == PoseState::Clean, "frozen LOD should keep pose clean");
    return ok;
}

bool TestValidationFailures()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    const SkeletonId skeleton{1};
    const AnimationClipId clip{1};
    const epidemic::runtime::animation::AnimatorInstanceId instance{1};
    const epidemic::runtime::animation::AnimatorHandle handle{instance, 1};
    bool ok = Expect(!SkeletonId{}.IsValid() && skeleton.IsValid() && skeleton == SkeletonId{1} &&
                         !AnimationClipId{}.IsValid() && clip.IsValid() && clip == AnimationClipId{1} &&
                         !epidemic::runtime::animation::AnimatorInstanceId{}.IsValid() && instance.IsValid() &&
                         !epidemic::runtime::animation::AnimatorHandle{}.IsValid() && handle.IsValid() &&
                         std::hash<SkeletonId>{}(skeleton) == std::hash<SkeletonId>{}(SkeletonId{1}) &&
                         std::hash<AnimationClipId>{}(clip) == std::hash<AnimationClipId>{}(AnimationClipId{1}) &&
                         std::hash<epidemic::runtime::animation::AnimatorInstanceId>{}(instance) ==
                             std::hash<epidemic::runtime::animation::AnimatorInstanceId>{}(epidemic::runtime::animation::AnimatorInstanceId{1}),
                     "animation public identity value contracts should be explicit");
    ok &= Expect(!runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{}, 1}).HasValue(), "invalid skeleton should fail");
    ok &= Expect(!runtime.RegisterSkeleton(SkeletonDesc{skeleton, 0}).HasValue(), "empty skeleton should fail");
    ok &= Expect(runtime.RegisterSkeleton(SkeletonDesc{skeleton, 1}).HasValue() && runtime.HasSkeleton(skeleton), "valid skeleton should register and be queryable");
    ok &= Expect(!runtime.RegisterSkeleton(SkeletonDesc{skeleton, 1}).HasValue(), "duplicate skeleton should fail");
    ok &= Expect(!runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{}, skeleton, AnimationLodLevel::Full}).HasValue(), "invalid owner should fail");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{clip, SkeletonId{99}, 1.0f}).HasValue(), "clip with missing skeleton should fail");
    ok &= Expect(runtime.RegisterClip(AnimationClipDesc{clip, skeleton, 1.0f}).HasValue() && runtime.HasClip(clip), "valid clip should register and be queryable");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{clip, skeleton, 1.0f}).HasValue(), "duplicate clip should fail");
    ok &= Expect(!runtime.IsFrozen() && runtime.Freeze().HasValue() && runtime.IsFrozen() && runtime.Freeze().HasValue(),
                 "animation registry freeze should be observable and idempotent");
    return ok;
}

bool TestNonFiniteNumericInputsAreRejected()
{
    const float nan_f = std::numeric_limits<float>::quiet_NaN();
    const float inf_f = std::numeric_limits<float>::infinity();
    const double nan_d = std::numeric_limits<double>::quiet_NaN();
    const double inf_d = std::numeric_limits<double>::infinity();

    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1}, 1}).HasValue(), "skeleton should register");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1}, SkeletonId{1}, nan_f}).HasValue(), "NaN clip duration should fail");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{2}, SkeletonId{1}, inf_f}).HasValue(), "+infinity clip duration should fail");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{3}, SkeletonId{1}, -inf_f}).HasValue(), "-infinity clip duration should fail");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{4}, SkeletonId{1}, 0.0f}).HasValue(), "zero clip duration should fail");
    ok &= Expect(runtime.RegisterClip(AnimationClipDesc{AnimationClipId{5}, SkeletonId{1}, std::numeric_limits<float>::min()}).HasValue(),
                 "smallest positive finite clip duration should pass");

    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "animator should create for playback validation");
    if (!animator)
    {
        return false;
    }
    ok &= Expect(!runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{5}, false, nan_d}).HasValue(), "NaN playback rate should fail");
    ok &= Expect(!runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{5}, false, inf_d}).HasValue(), "+infinity playback rate should fail");
    ok &= Expect(!runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{5}, false, -inf_d}).HasValue(), "-infinity playback rate should fail");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{5}, false, 0.0}).HasValue(), "zero playback rate should remain valid");

    auto resources = std::make_shared<TestResourceSource>();
    resources->clip_duration = nan_f;
    AnimationRuntime resource_runtime{AnimationOptions{.enable_mock_pose_evaluation = true}, AnimationDependencies{resources, {}, std::make_shared<TestEvaluatorBackend>()}};
    const auto resource_animator = resource_runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{88}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(resource_animator.HasValue(), "resource-backed animator should create");
    if (resource_animator)
    {
        ok &= Expect(!resource_runtime.Play(AnimationPlaybackCommand{resource_animator.Value(), AnimationClipId{10}}).HasValue(),
                     "resource source must not inject non-finite clip duration");
    }
    return ok;
}

bool TestHandlePlaybackPoseBufferAndFactory()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto handle = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(handle.HasValue(), "handle animator should be created");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{handle.Value(), AnimationClipId{10}, false}).HasValue(), "handle play should succeed");
    ok &= Expect(runtime.Pause(handle.Value()).HasValue(), "pause should succeed");
    ok &= ExpectPlayback(runtime, handle.Value(), AnimatorPlaybackState::Paused, "pause should update state");
    ok &= Expect(runtime.Crossfade(handle.Value(), AnimationClipId{11}, FrameDuration{std::chrono::microseconds{1000000}}).HasValue(), "crossfade should succeed");
    ok &= ExpectPlayback(runtime, handle.Value(), AnimatorPlaybackState::Blending, "crossfade should blend");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{500000}}, 1, 1), "delta tick should advance one animator");

    const auto pose = runtime.GetPoseBuffer(handle.Value());
    ok &= Expect(pose.HasValue() && pose.Value().animator == handle.Value(), "pose buffer should preserve handle");
    ok &= Expect(pose.HasValue() && pose.Value().bone_transforms.size() == 32, "pose buffer should contain skeleton bone transforms");
    ok &= Expect(pose.HasValue() && !pose.Value().bone_transforms.empty() && pose.Value().bone_transforms.front().position.z > 0.49f &&
                     pose.Value().bone_transforms.front().position.z < 0.51f,
                 "crossfade should publish halfway target weight");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{500000}}, 1, 1), "second delta tick should complete crossfade");
    ok &= ExpectPlayback(runtime, handle.Value(), AnimatorPlaybackState::Playing, "completed crossfade should continue target playback");
    ok &= Expect(runtime.Stop(handle.Value()).HasValue(), "stop should succeed");

    const auto services = CreateAnimationServices(AnimationOptions{}, AnimationDependencies{{}, {}, std::make_shared<TestEvaluatorBackend>()});
    ok &= Expect(services.HasValue(), "production animation services should be created");
    ok &= Expect(services.HasValue() && services.Value().skeletons != nullptr && services.Value().clips != nullptr &&
                     services.Value().runtime != nullptr && services.Value().poses != nullptr && services.Value().events != nullptr,
                 "animation services should be populated");
    return ok;
}

bool TestPoseQueriesRejectStaleHandle()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "animator should create");
    if (!animator)
    {
        return false;
    }

    auto stale = animator.Value();
    ++stale.generation;
    const auto state = runtime.GetPoseState(stale);
    const auto snapshot = runtime.GetPoseSnapshot(stale);
    const auto buffer = runtime.GetPoseBuffer(stale);
    return ok && !state && state.GetError().HasCode("animation.animator_not_found") &&
           !snapshot && snapshot.GetError().HasCode("animation.animator_not_found") &&
           !buffer && buffer.GetError().HasCode("animation.animator_not_found");
}

bool TestPauseRejectsStoppedAndFinished()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "animator should create");
    if (!animator)
    {
        return false;
    }

    const auto stopped_pause = runtime.Pause(animator.Value());
    ok &= Expect(!stopped_pause && stopped_pause.GetError().HasCode("animation.invalid_playback_transition"),
                 "stopped animator should not pause");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}}).HasValue(), "animator should play");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{1000000}}, 1, 1), "animator should finish");
    const auto finished_pause = runtime.Pause(animator.Value());
    return ok && Expect(!finished_pause && finished_pause.GetError().HasCode("animation.invalid_playback_transition"),
                        "finished animator should not pause");
}

bool TestCrossfadeWithoutCurrentClipStartsTargetWithoutBlend()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "animator should create");
    if (!animator)
    {
        return false;
    }

    const auto crossfade = runtime.Crossfade(animator.Value(), AnimationClipId{10}, FrameDuration{std::chrono::microseconds{1000000}});
    const auto snapshot = runtime.GetAnimatorSnapshot(animator.Value());
    return ok && crossfade && snapshot && snapshot.Value().playback == AnimatorPlaybackState::Playing &&
           runtime.Events().size() == 1u && runtime.Events().front().name == "animation.started";
}

bool TestLoopSettingIsolatedPerAnimatorAndClipMetadataImmutable()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto first = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    const auto second = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{78}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(first.HasValue() && second.HasValue(), "animators should be created");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{first.Value(), AnimationClipId{10}, true}).HasValue(),
                 "first animator should play looping instance");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{second.Value(), AnimationClipId{10}, false}).HasValue(),
                 "second animator should play non-looping instance");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{1000000}}, 2, 2), "both animators should tick");
    ok &= ExpectPlayback(runtime, first.Value(), AnimatorPlaybackState::Playing, "looping instance should keep playing");
    ok &= ExpectPlayback(runtime, second.Value(), AnimatorPlaybackState::Finished, "non-looping instance should finish");
    const auto first_snapshot = runtime.GetAnimatorSnapshot(first.Value());
    ok &= Expect(first_snapshot.HasValue() && first_snapshot.Value().local_time.value.count() == 0,
                 "looping instance should wrap local time at clip duration");
    ok &= Expect(runtime.Events().size() == 4 && runtime.Events().back().name == "animation.finished",
                 "loop and finish events should be queued deterministically");

    const auto third = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{79}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(third.HasValue(), "third animator should be created");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{third.Value(), AnimationClipId{10}, false}).HasValue(),
                 "clip metadata should not have been mutated to loop");
    ok &= Expect(TickAtLeast(runtime, FrameDuration{std::chrono::microseconds{1000000}}, 3, 1), "third animator should tick");
    ok &= ExpectPlayback(runtime, third.Value(), AnimatorPlaybackState::Finished, "third non-looping instance should finish");
    return ok;
}

bool TestResourceSourceFailureAndPoseSinkPublication()
{
    auto resources = std::make_shared<TestResourceSource>();
    auto sink = std::make_shared<TestPoseSink>();
    auto evaluator = std::make_shared<TestEvaluatorBackend>();
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}, AnimationDependencies{resources, sink, evaluator}};

    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    bool ok = Expect(animator.HasValue(), "resource-backed skeleton should create animator");
    resources->fail_clip = true;
    ok &= Expect(!runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}, false}).HasValue(),
                 "resource source clip failure should propagate");
    ok &= ExpectReadiness(runtime, animator.Value(), AnimatorReadiness::Ready, "resource failure should leave animator readiness unchanged");

    resources->fail_clip = false;
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}, false}).HasValue(),
                 "resource-backed clip should play after failure clears");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{500000}}, 1, 1), "resource-backed animator should tick");
    ok &= Expect(!sink->published.empty(), "pose sink should receive immutable pose buffer");
    ok &= Expect(sink->published.back() != nullptr && sink->published.back()->bone_transforms.size() == 16,
                 "published pose should use resource-backed skeleton");
    static_assert(std::is_same_v<decltype(sink->published.back()), std::shared_ptr<const PoseBuffer>&>);
    return ok;
}

bool TestFractionalPlaybackRateAccumulates()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "animator should create");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}, false, 0.5}).HasValue(),
                 "fractional playback should start");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{1}}, 1, 1), "first fractional tick should advance");
    ok &= Expect(TickValue(runtime, FrameDuration{std::chrono::microseconds{1}}, 1, 1), "second fractional tick should advance");
    const auto pose = runtime.GetPoseBuffer(animator.Value());
    return ok && Expect(pose.HasValue() && !pose.Value().bone_transforms.empty() && pose.Value().bone_transforms.front().position.x > 0.0f,
                        "fractional playback should eventually advance local time");
}

bool TestPoseSinkFailurePropagates()
{
    auto sink = std::make_shared<TestPoseSink>();
    auto evaluator = std::make_shared<TestEvaluatorBackend>();
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}, AnimationDependencies{{}, sink, evaluator}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "animator should create");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}}).HasValue(), "animator should play");
    sink->fail = true;
    const auto tick = runtime.Tick(FrameDuration{std::chrono::microseconds{1000}}, 1);
    const auto after = runtime.GetAnimatorSnapshot(animator.Value());
    return ok && Expect(!tick.HasValue() && tick.GetError().HasCode("animation.publish_failed") &&
                        after.HasValue() && after.Value().revision == 2 && after.Value().local_time.IsZero(),
                        "pose sink failure should preserve animator pre-state and propagate from tick");
}

bool TestEventCapacityDropsOldest()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true, .event_capacity = 1}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "animator should create");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}}).HasValue(), "animator should play");
    ok &= Expect(runtime.Stop(animator.Value()).HasValue(), "animator should stop");
    return ok && Expect(runtime.Events().size() == 1 && runtime.Events().front().name == "animation.stopped",
                        "event capacity should retain the newest event");
}

bool TestZeroEventCapacityUsesDefaultBound()
{
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true, .event_capacity = 0}};
    bool ok = Expect(SeedResources(runtime), "resources should seed for zero event capacity");
    for (std::uint64_t index = 0; index < 70; ++index)
    {
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{100 + index}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(animator.HasValue(), "animator should create for zero event capacity");
        if (animator)
        {
            ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}}).HasValue(),
                         "play should queue event under default event capacity");
        }
    }
    return ok && Expect(runtime.Events().size() == AnimationOptions{}.event_capacity,
                        "zero event capacity should use the default bounded capacity");
}

bool TestFactoryProfilesSeparateMockEvaluation()
{
    auto sink = std::make_shared<TestPoseSink>();
    auto resources = std::make_shared<TestResourceSource>();
    auto evaluator = std::make_shared<TestEvaluatorBackend>();
    const auto missing_evaluator = CreateAnimationServices(
        AnimationOptions{.enable_mock_pose_evaluation = true},
        AnimationDependencies{resources, sink, {}});
    if (!Expect(!missing_evaluator.HasValue() && missing_evaluator.GetError().HasCode("animation.evaluator_missing"),
                "production animation factory should require evaluator backend"))
    {
        return false;
    }
    const auto production = CreateAnimationServices(
        AnimationOptions{.enable_mock_pose_evaluation = true},
        AnimationDependencies{resources, sink, evaluator});
    if (!Expect(production.HasValue(), "production animation factory should succeed"))
    {
        return false;
    }

    const auto animator = production.Value().runtime->CreateAnimatorHandle(
        AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    bool ok = Expect(animator.HasValue(), "production resource-backed animator should create");
    ok &= Expect(production.Value().runtime->Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}, false}).HasValue(),
                 "production animator should play");
    ok &= Expect(TickValue(*production.Value().runtime, FrameDuration{std::chrono::microseconds{500000}}, 1, 1), "production animator should tick");
    ok &= Expect(!sink->published.empty(), "production evaluator should publish pose");

    const auto reference = CreateReferenceAnimationServices(AnimationOptions{});
    const auto mock = CreateMockAnimationServices(AnimationOptions{});
    ok &= Expect(reference.runtime != nullptr && reference.poses != nullptr, "reference services should be populated");
    ok &= Expect(mock.runtime != nullptr && mock.poses != nullptr, "mock services should be populated");
    return ok;
}

bool TestPortablePlaybackScalingBoundaries()
{
    bool ok = true;

    auto make_runtime = [] {
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
        const bool seeded = runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1}, 1}).HasValue() &&
                            runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1}, SkeletonId{1}, 1000.0f}).HasValue();
        return std::pair<AnimationRuntime, bool>{std::move(runtime), seeded};
    };

    {
        auto [runtime, seeded] = make_runtime();
        ok &= Expect(seeded, "portable scaling boundary resources should seed");
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{601}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{1}, false, 1.0}).HasValue(),
                     "exact-rate boundary animator should start");
        const auto tick = runtime.Tick(FrameDuration{std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()}}, 1);
        const auto after = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(tick && after && after.Value().playback == AnimatorPlaybackState::Finished &&
                         after.Value().local_time == FrameDuration{std::chrono::microseconds{1'000'000'000}},
                     "INT64_MAX * 1.0 must remain a valid scaled delta and follow normal clip completion");
    }

    {
        auto [runtime, seeded] = make_runtime();
        ok &= Expect(seeded, "first-overflow resources should seed");
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{602}, SkeletonId{1}, AnimationLodLevel::Full});
        const double overflow_rate = std::nextafter(1.0, 2.0);
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{1}, false, overflow_rate}).HasValue(),
                     "first-overflow animator should start");
        const auto before = runtime.GetAnimatorSnapshot(animator.Value());
        const auto failed = runtime.Tick(FrameDuration{std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()}}, 1);
        const auto after = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(!failed && failed.GetError().HasCode("animation.time_overflow") && before && after &&
                         before.Value().local_time == after.Value().local_time && before.Value().revision == after.Value().revision,
                     "first representable playback rate above 1.0 must reject atomically at INT64_MAX");
    }

    {
        auto [runtime, seeded] = make_runtime();
        ok &= Expect(seeded, "fractional-boundary resources should seed");
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{603}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{1}, false, 1.0}).HasValue(),
                     "fractional-boundary animator should start");
        runtime.SetFractionalMicrosecondsForTesting(animator.Value(), 0.5);
        const auto before = runtime.GetAnimatorSnapshot(animator.Value());
        const auto failed = runtime.Tick(FrameDuration{std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()}}, 1);
        const auto after = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(!failed && failed.GetError().HasCode("animation.time_overflow") && before && after &&
                         before.Value().local_time == after.Value().local_time && before.Value().revision == after.Value().revision,
                     "non-zero carried fraction must reject an otherwise exact INT64_MAX boundary atomically");
    }

    {
        auto [runtime, seeded] = make_runtime();
        ok &= Expect(seeded, "fractional carry resources should seed");
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{604}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{1}, true, 0.1}).HasValue(),
                     "fractional carry animator should start");
        runtime.SetFractionalMicrosecondsForTesting(animator.Value(), std::nextafter(1.0, 0.0));
        const auto tick = runtime.Tick(FrameDuration{std::chrono::microseconds{10}}, 1);
        const auto after = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(tick && after && after.Value().local_time == FrameDuration{std::chrono::microseconds{1}},
                     "rounded fractional addition must not create a false carry across a microsecond boundary");
    }

    {
        auto [runtime, seeded] = make_runtime();
        ok &= Expect(seeded, "post-carry overflow resources should seed");
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{605}, SkeletonId{1}, AnimationLodLevel::Full});
        const double overflow_rate = std::nextafter(1.0, 2.0);
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{1}, false, overflow_rate}).HasValue(),
                     "post-carry overflow animator should start");
        runtime.SetFractionalMicrosecondsForTesting(animator.Value(), 0.75);
        const auto before = runtime.GetAnimatorSnapshot(animator.Value());
        const auto failed = runtime.Tick(FrameDuration{std::chrono::microseconds{std::numeric_limits<std::int64_t>::max() - 2048}}, 1);
        const auto after = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(!failed && failed.GetError().HasCode("animation.time_overflow") && before && after &&
                         before.Value().local_time == after.Value().local_time && before.Value().revision == after.Value().revision,
                     "fractional carry into INT64_MAX with a positive residual must reject atomically");
    }

    {
        auto [runtime, seeded] = make_runtime();
        ok &= Expect(seeded, "exact remainder rounding resources should seed");
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{606}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{1}, false, 0.1}).HasValue(),
                     "exact remainder rounding animator should start");
        runtime.SetFractionalMicrosecondsForTesting(animator.Value(), 0x1.55c5bddc376f8p-4);
        const auto tick = runtime.Tick(FrameDuration{std::chrono::microseconds{1948608813260436178LL}}, 1);
        ok &= Expect(tick && runtime.FractionalMicrosecondsForTesting(animator.Value()) == 0x1.6699d4b59816ap-1,
                     "fractional remainder must be rounded once from the exact binary sum");
    }

    return ok;
}

bool TestStatefulPlaybackRemainderPartitionAndFailureAtomicity()
{
    bool ok = true;
    constexpr std::int64_t kFirstDelta = 36028797018963948LL;
    constexpr std::int64_t kSmallDelta = 10LL;
    constexpr std::int64_t kCombinedDelta = 36028797018963968LL;
    constexpr std::int64_t kExpectedFirst = 3602879701896394LL;
    constexpr std::int64_t kExpectedSecond = 3602879701896395LL;
    constexpr std::int64_t kExpectedFinal = 3602879701896397LL;

    auto seed_long_clip = [](AnimationRuntime& runtime) {
        return runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1}, 1}).HasValue() &&
               runtime.RegisterClip(AnimationClipDesc{AnimationClipId{71}, SkeletonId{1}, 5.0e9f}).HasValue() &&
               runtime.RegisterClip(AnimationClipDesc{AnimationClipId{72}, SkeletonId{1}, 5.0e9f}).HasValue();
    };

    {
        AnimationRuntime partitioned{AnimationOptions{.enable_mock_pose_evaluation = true}};
        AnimationRuntime combined{AnimationOptions{.enable_mock_pose_evaluation = true}};
        ok &= Expect(seed_long_clip(partitioned) && seed_long_clip(combined), "stateful remainder resources should seed");
        const auto partitioned_animator = partitioned.CreateAnimatorHandle(
            AnimatorDesc{RuntimeObjectId{701}, SkeletonId{1}, AnimationLodLevel::Full});
        const auto combined_animator = combined.CreateAnimatorHandle(
            AnimatorDesc{RuntimeObjectId{702}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(partitioned_animator && combined_animator &&
                         partitioned.Play(AnimationPlaybackCommand{partitioned_animator.Value(), AnimationClipId{71}, false, 0.1}).HasValue() &&
                         combined.Play(AnimationPlaybackCommand{combined_animator.Value(), AnimationClipId{71}, false, 0.1}).HasValue(),
                     "partitioned and combined animators should start");

        const auto first_tick = partitioned.Tick(FrameDuration{std::chrono::microseconds{kFirstDelta}}, 1);
        const auto first = partitioned.GetAnimatorSnapshot(partitioned_animator.Value());
        ok &= Expect(first_tick && first && first.Value().local_time.value.count() == kExpectedFirst &&
                         partitioned.HasExactFractionalMicrosecondsForTesting(partitioned_animator.Value()),
                     "first partition must retain an exact non-zero fractional remainder");

        const auto second_tick = partitioned.Tick(FrameDuration{std::chrono::microseconds{kSmallDelta}}, 1);
        const auto second = partitioned.GetAnimatorSnapshot(partitioned_animator.Value());
        ok &= Expect(second_tick && second && second.Value().local_time.value.count() == kExpectedSecond,
                     "second partition must advance to the exact represented-rate floor");

        const auto third_tick = partitioned.Tick(FrameDuration{std::chrono::microseconds{kSmallDelta}}, 1);
        const auto partitioned_final = partitioned.GetAnimatorSnapshot(partitioned_animator.Value());
        const auto combined_tick = combined.Tick(FrameDuration{std::chrono::microseconds{kCombinedDelta}}, 1);
        const auto combined_final = combined.GetAnimatorSnapshot(combined_animator.Value());
        ok &= Expect(third_tick && combined_tick && partitioned_final && combined_final &&
                         partitioned_final.Value().local_time.value.count() == kExpectedFinal &&
                         combined_final.Value().local_time.value.count() == kExpectedFinal &&
                         partitioned_final.Value().local_time == combined_final.Value().local_time,
                     "partitioned and combined playback must preserve the same exact represented-rate accumulation");
    }

    auto verify_failure_atomicity = [&](bool evaluator_result_failure,
                                        bool evaluator_exception,
                                        bool sink_result_failure,
                                        bool sink_exception,
                                        std::string_view label) {
        auto evaluator = std::make_shared<TestEvaluatorBackend>();
        auto sink = std::make_shared<TestPoseSink>();
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}, AnimationDependencies{{}, sink, evaluator}};
        bool case_ok = Expect(seed_long_clip(runtime), "failure-atomicity resources should seed");
        const auto animator = runtime.CreateAnimatorHandle(
            AnimatorDesc{RuntimeObjectId{703}, SkeletonId{1}, AnimationLodLevel::Full});
        case_ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{71}, false, 0.1}).HasValue(),
                          "failure-atomicity animator should start");
        case_ok &= Expect(runtime.Tick(FrameDuration{std::chrono::microseconds{kFirstDelta}}, 1).HasValue(),
                          "failure-atomicity setup tick should succeed");
        const auto before = runtime.GetAnimatorSnapshot(animator.Value());
        const double remainder_before = runtime.FractionalMicrosecondsForTesting(animator.Value());
        const auto exact_before = runtime.ExactFractionalMicrosecondsForTesting(animator.Value());

        evaluator->fail = evaluator_result_failure;
        evaluator->throw_on_evaluate = evaluator_exception;
        sink->fail = sink_result_failure;
        sink->throw_on_publish = sink_exception;
        const auto failed = runtime.Tick(FrameDuration{std::chrono::microseconds{kSmallDelta}}, 1);
        const auto after = runtime.GetAnimatorSnapshot(animator.Value());
        case_ok &= Expect(!failed && before && after &&
                              before.Value().local_time == after.Value().local_time &&
                              before.Value().revision == after.Value().revision &&
                              remainder_before == runtime.FractionalMicrosecondsForTesting(animator.Value()) &&
                              exact_before == runtime.ExactFractionalMicrosecondsForTesting(animator.Value()),
                          label);

        evaluator->fail = false;
        evaluator->throw_on_evaluate = false;
        sink->fail = false;
        sink->throw_on_publish = false;
        case_ok &= Expect(runtime.Tick(FrameDuration{std::chrono::microseconds{kSmallDelta}}, 1).HasValue() &&
                              runtime.Tick(FrameDuration{std::chrono::microseconds{kSmallDelta}}, 1).HasValue(),
                          "retry after staged failure should accept each playback delta exactly once");
        const auto recovered = runtime.GetAnimatorSnapshot(animator.Value());
        case_ok &= Expect(recovered && recovered.Value().local_time.value.count() == kExpectedFinal,
                          "retry after staged failure must match the clean partitioned execution");
        return case_ok;
    };

    ok &= verify_failure_atomicity(true, false, false, false,
                                   "evaluator Result failure must preserve local time, revision and exact fractional remainder");
    ok &= verify_failure_atomicity(false, true, false, false,
                                   "evaluator exception must preserve local time, revision and exact fractional remainder");
    ok &= verify_failure_atomicity(false, false, true, false,
                                   "pose-sink Result failure must preserve local time, revision and exact fractional remainder");
    ok &= verify_failure_atomicity(false, false, false, true,
                                   "pose-sink exception must preserve local time, revision and exact fractional remainder");

    {
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}};
        ok &= Expect(seed_long_clip(runtime), "remainder reset resources should seed");
        const auto animator = runtime.CreateAnimatorHandle(
            AnimatorDesc{RuntimeObjectId{704}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{71}, false, 0.1}).HasValue() &&
                         runtime.Tick(FrameDuration{std::chrono::microseconds{kFirstDelta}}, 1).HasValue() &&
                         runtime.HasExactFractionalMicrosecondsForTesting(animator.Value()),
                     "reset test should first establish an exact fractional remainder");

        ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{71}, false, 0.1}).HasValue() &&
                         !runtime.HasExactFractionalMicrosecondsForTesting(animator.Value()) &&
                         runtime.FractionalMicrosecondsForTesting(animator.Value()) == 0.0,
                     "Play must reset both exact and mirrored fractional playback state");
        ok &= Expect(runtime.Tick(FrameDuration{std::chrono::microseconds{kSmallDelta}}, 1).HasValue(),
                     "play-reset animator should tick");
        const auto after_play_reset = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(after_play_reset && after_play_reset.Value().local_time.value.count() == 1,
                     "Play reset must prevent the previous clip remainder from carrying into the new playback");

        ok &= Expect(runtime.Tick(FrameDuration{std::chrono::microseconds{kFirstDelta}}, 1).HasValue() &&
                         runtime.HasExactFractionalMicrosecondsForTesting(animator.Value()),
                     "Stop reset test should establish a new exact remainder");
        ok &= Expect(runtime.Stop(animator.Value()).HasValue() &&
                         !runtime.HasExactFractionalMicrosecondsForTesting(animator.Value()) &&
                         runtime.FractionalMicrosecondsForTesting(animator.Value()) == 0.0,
                     "Stop must clear both exact and mirrored fractional playback state");

        ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{71}, false, 0.1}).HasValue() &&
                         runtime.Tick(FrameDuration{std::chrono::microseconds{kFirstDelta}}, 1).HasValue() &&
                         runtime.HasExactFractionalMicrosecondsForTesting(animator.Value()),
                     "immediate replacement test should establish an exact remainder");
        ok &= Expect(runtime.Crossfade(animator.Value(), AnimationClipId{72}, FrameDuration{}).HasValue() &&
                         !runtime.HasExactFractionalMicrosecondsForTesting(animator.Value()) &&
                         runtime.FractionalMicrosecondsForTesting(animator.Value()) == 0.0,
                     "zero-duration Crossfade must not inherit the replaced clip fractional remainder");
    }

    return ok;
}

bool TestDeepFreezeAnimationContracts()
{
    bool ok = true;
    {
        auto evaluator = std::make_shared<TestEvaluatorBackend>();
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}, AnimationDependencies{{}, {}, evaluator}};
        ok &= Expect(SeedResources(runtime), "resources should seed for evaluator atomicity");
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{501}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{11}, true}).HasValue(), "animator should start for evaluator atomicity");
        const auto before = runtime.GetAnimatorSnapshot(animator.Value());
        const auto before_pose = runtime.GetPoseBuffer(animator.Value());
        evaluator->fail = true;
        const auto failed = runtime.Tick(FrameDuration{std::chrono::microseconds{1000}}, 1);
        const auto after = runtime.GetAnimatorSnapshot(animator.Value());
        const auto after_pose = runtime.GetPoseBuffer(animator.Value());
        ok &= Expect(!failed && before && after && before.Value().local_time == after.Value().local_time && before.Value().revision == after.Value().revision,
                     "evaluator Result failure must leave animator snapshot unchanged");
        ok &= Expect(before_pose && after_pose && before_pose.Value().revision == after_pose.Value().revision, "evaluator failure must preserve cached pose");
        evaluator->fail = false;
        ok &= Expect(runtime.Tick(FrameDuration{std::chrono::microseconds{1000}}, 1).HasValue(), "retry after evaluator recovery should advance once");
        const auto recovered = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(recovered && recovered.Value().local_time.value.count() == 1000, "recovered retry should advance exactly one delta");
        evaluator->throw_on_evaluate = true;
        const auto throw_before = runtime.GetAnimatorSnapshot(animator.Value());
        const auto thrown = runtime.Tick(FrameDuration{std::chrono::microseconds{1000}}, 1);
        const auto throw_after = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(!thrown && thrown.GetError().HasCode("animation.evaluator_exception") && throw_before.Value().local_time == throw_after.Value().local_time,
                     "throwing evaluator must preserve staged animator state");
    }
    {
        auto sink = std::make_shared<TestPoseSink>(); auto evaluator = std::make_shared<TestEvaluatorBackend>();
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}, AnimationDependencies{{}, sink, evaluator}};
        ok &= Expect(SeedResources(runtime), "resources should seed for sink semantics");
        const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{502}, SkeletonId{1}, AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{11}, true}).HasValue(), "animator should play for sink semantics");
        const auto before = runtime.GetAnimatorSnapshot(animator.Value());
        const auto events_before = runtime.Events().size();
        sink->fail = true;
        const auto failed = runtime.Tick(FrameDuration{std::chrono::microseconds{1000}}, 1);
        const auto after = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(!failed && failed.GetError().HasCode("animation.publish_failed") && after.Value().revision == before.Value().revision && after.Value().local_time == before.Value().local_time && runtime.Events().size() == events_before,
                     "pose sink Result failure must preserve animator, cached event state and revision");
        sink->fail = false; sink->throw_on_publish = true;
        const auto throw_before = runtime.GetAnimatorSnapshot(animator.Value());
        const auto thrown = runtime.Tick(FrameDuration{std::chrono::microseconds{1000}}, 1);
        const auto throw_after = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(!thrown && thrown.GetError().HasCode("animation.publish_exception") && throw_before.Value().revision == throw_after.Value().revision && throw_before.Value().local_time == throw_after.Value().local_time,
                     "throwing sink must preserve animator pre-state and remain inside public Result boundary");
        sink->throw_on_publish = false;
        ok &= Expect(runtime.Tick(FrameDuration{std::chrono::microseconds{1000}}, 1).HasValue(), "retry after sink recovery should commit once");
        const auto recovered = runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(recovered.Value().local_time.value.count() == 1000 && recovered.Value().revision == before.Value().revision + 1,
                     "sink retry should match one clean tick");
    }
    {
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}}; ok &= Expect(SeedResources(runtime), "resources should seed for zero crossfade");
        const auto animator=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{503},SkeletonId{1},AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(),AnimationClipId{11},true}).HasValue() && runtime.Tick(FrameDuration{std::chrono::microseconds{1000}},1).HasValue(), "animator should have ready pose");
        ok &= Expect(runtime.GetPoseState(animator.Value()).Value()==PoseState::Ready, "pre-crossfade pose should be ready");
        ok &= Expect(runtime.Crossfade(animator.Value(),AnimationClipId{10},FrameDuration{}).HasValue(), "zero crossfade should succeed");
        const auto pose=runtime.GetPoseState(animator.Value()); ok &= Expect(pose && (pose.Value()==PoseState::Dirty || pose.Value()==PoseState::Evaluating), "zero crossfade must invalidate old clip pose");
    }
    {
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true, .max_animators = 1, .max_skeleton_joints = 8}};
        ok &= Expect(runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1},8}).HasValue(), "maximum skeleton joint count should register");
        ok &= Expect(!runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{2},9}), "oversized skeleton should be rejected");
        ok &= Expect(runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1},SkeletonId{1},1.0f}).HasValue(), "clip should register");
        const auto invalid_lod=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{1},SkeletonId{1},static_cast<AnimationLodLevel>(99)}); ok &= Expect(!invalid_lod,"invalid LOD create should fail");
        const auto animator=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{1},SkeletonId{1},AnimationLodLevel::Full}); ok &= Expect(animator.HasValue(),"valid animator should create");
        ok &= Expect(!runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{2},SkeletonId{1},AnimationLodLevel::Full}),"max_animators should be enforced");
        const auto before=runtime.GetAnimatorSnapshot(animator.Value()); ok &= Expect(!runtime.SetLod(animator.Value(),static_cast<AnimationLodLevel>(99)),"invalid SetLod should fail"); const auto after=runtime.GetAnimatorSnapshot(animator.Value()); ok &= Expect(before.Value().revision==after.Value().revision && before.Value().lod==after.Value().lod,"invalid SetLod should not mutate state");
        ok &= Expect(runtime.Freeze().HasValue() && runtime.Freeze().HasValue() && runtime.IsFrozen(),"animation registry freeze should be idempotent");
        ok &= Expect(!runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{3},1}) && !runtime.RegisterClip(AnimationClipDesc{AnimationClipId{3},SkeletonId{1},1.0f}),"registration after freeze should fail");
    }
    {
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}}; ok &= Expect(SeedResources(runtime),"resources should seed for revision exhaustion");
        const auto animator=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{504},SkeletonId{1},AnimationLodLevel::Full}); runtime.SetRevisionForTesting(animator.Value(),std::numeric_limits<std::uint64_t>::max()-1);
        ok &= Expect(runtime.SetLod(animator.Value(),AnimationLodLevel::Reduced).HasValue(),"last animator revision should commit"); const auto at_max=runtime.GetAnimatorSnapshot(animator.Value());
        const auto exhausted=runtime.SetLod(animator.Value(),AnimationLodLevel::Full); const auto unchanged=runtime.GetAnimatorSnapshot(animator.Value()); ok &= Expect(!exhausted && exhausted.GetError().HasCode("animation.revision_exhausted") && at_max.Value().revision==unchanged.Value().revision && at_max.Value().lod==unchanged.Value().lod,"revision exhaustion should not wrap or mutate");
    }
    {
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}}; ok &= Expect(SeedResources(runtime),"resources should seed for id exhaustion"); runtime.SetNextIdentityForTesting(std::numeric_limits<std::uint64_t>::max(),std::numeric_limits<std::uint32_t>::max());
        const auto last=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{505},SkeletonId{1},AnimationLodLevel::Full}); ok &= Expect(last && last.Value().id.value==std::numeric_limits<std::uint64_t>::max() && last.Value().generation==std::numeric_limits<std::uint32_t>::max(),"final animator identity should issue once"); ok &= Expect(!runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{506},SkeletonId{1},AnimationLodLevel::Full}),"animator identity should exhaust after final committed create");
    }
    {
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}}; ok &= Expect(runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1},1}).HasValue(),"skeleton should register for numeric tests"); ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1},SkeletonId{1},FLT_MAX}),"FLT_MAX clip duration should fail checked conversion"); ok &= Expect(runtime.RegisterClip(AnimationClipDesc{AnimationClipId{2},SkeletonId{1},1000.0f}).HasValue(),"normal clip should register"); const auto animator=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{507},SkeletonId{1},AnimationLodLevel::Full});
        ok &= Expect(!runtime.Play(AnimationPlaybackCommand{animator.Value(),AnimationClipId{2},false,std::numeric_limits<double>::max()}),"DBL_MAX playback rate should fail before mutation"); ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(),AnimationClipId{2},true,2.0}).HasValue(),"finite playback should start"); const auto before=runtime.GetAnimatorSnapshot(animator.Value()); const auto overflow=runtime.Tick(FrameDuration{std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()}},1); const auto after=runtime.GetAnimatorSnapshot(animator.Value()); ok &= Expect(!overflow && overflow.GetError().HasCode("animation.time_overflow") && before.Value().local_time==after.Value().local_time && before.Value().revision==after.Value().revision,"playback time overflow must leave animator unchanged");
    }
    {
        auto resources=std::make_shared<TestResourceSource>(); resources->skeleton_joint_count=9; AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation=true,.max_skeleton_joints=8},AnimationDependencies{resources,{},{}}}; const auto created=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{508},SkeletonId{1},AnimationLodLevel::Full}); ok &= Expect(!created && created.GetError().HasCode("animation.invalid_skeleton"),"oversized resource-backed skeleton should be rejected without allocation");
    }
    {
        auto evaluator=std::make_shared<TestEvaluatorBackend>(); evaluator->wrong_owner=true;
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation=true},AnimationDependencies{{},{},evaluator}};
        ok &= Expect(SeedResources(runtime),"resources should seed for pose identity validation");
        const auto animator=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{509},SkeletonId{1},AnimationLodLevel::Full});
        ok &= Expect(animator && runtime.Play(AnimationPlaybackCommand{animator.Value(),AnimationClipId{11},true}).HasValue(),"animator should start for invalid evaluator pose");
        const auto before=runtime.GetAnimatorSnapshot(animator.Value());
        const auto failed=runtime.Tick(FrameDuration{std::chrono::microseconds{1000}},1);
        const auto after=runtime.GetAnimatorSnapshot(animator.Value());
        ok &= Expect(!failed && failed.GetError().HasCode("animation.invalid_pose") && before.Value().revision==after.Value().revision && before.Value().local_time==after.Value().local_time,
                     "evaluator pose for wrong owner must be rejected before animator publication");
    }
    {
        AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation=true,.event_capacity=2}};
        ok &= Expect(SeedResources(runtime),"resources should seed for stale-handle matrix");
        const auto animator=runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{510},SkeletonId{1},AnimationLodLevel::Full});
        ok &= Expect(animator.HasValue() && runtime.DestroyAnimator(animator.Value()).HasValue(),"animator should destroy for stale-handle matrix");
        ok &= Expect(!runtime.Play(AnimationPlaybackCommand{animator.Value(),AnimationClipId{11},true}) && !runtime.Pause(animator.Value()) && !runtime.Stop(animator.Value()) &&
                     !runtime.Crossfade(animator.Value(),AnimationClipId{11},FrameDuration{std::chrono::microseconds{1}}) && !runtime.SetLod(animator.Value(),AnimationLodLevel::Reduced) &&
                     !runtime.GetAnimatorSnapshot(animator.Value()) && !runtime.GetPoseState(animator.Value()) && !runtime.GetPoseSnapshot(animator.Value()) && !runtime.GetPoseBuffer(animator.Value()),
                     "destroyed animator handle must be rejected by every mutator and query");
    }
    return ok;
}

} // namespace

int main()
{
    static_assert(std::is_abstract_v<IAnimationResourceSource>);
    static_assert(std::is_abstract_v<IAnimationPoseSink>);
    static_assert(std::is_abstract_v<IAnimationEvaluatorBackend>);

    bool ok = true;
    ok &= TestCreateDestroyAnimator();
    ok &= TestPlayInvalidClipReturnsError();
    ok &= TestStateTransitionsAndEvents();
    ok &= TestTickOrderIsDeterministic();
    ok &= TestLodPlaceholderChangesPoseState();
    ok &= TestValidationFailures();
    ok &= TestNonFiniteNumericInputsAreRejected();
    ok &= TestHandlePlaybackPoseBufferAndFactory();
    ok &= TestPoseQueriesRejectStaleHandle();
    ok &= TestPauseRejectsStoppedAndFinished();
    ok &= TestCrossfadeWithoutCurrentClipStartsTargetWithoutBlend();
    ok &= TestLoopSettingIsolatedPerAnimatorAndClipMetadataImmutable();
    ok &= TestResourceSourceFailureAndPoseSinkPublication();
    ok &= TestFractionalPlaybackRateAccumulates();
    ok &= TestPoseSinkFailurePropagates();
    ok &= TestEventCapacityDropsOldest();
    ok &= TestZeroEventCapacityUsesDefaultBound();
    ok &= TestFactoryProfilesSeparateMockEvaluation();
    ok &= TestPortablePlaybackScalingBoundaries();
    ok &= TestStatefulPlaybackRemainderPartitionAndFailureAtomicity();
    ok &= TestDeepFreezeAnimationContracts();
    return ok ? 0 : 1;
}



