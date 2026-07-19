#include "animation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <chrono>
#include <iostream>
#include <memory>
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

    epidemic::foundation::Result<SkeletonDesc> LoadSkeleton(SkeletonId id) const override
    {
        if (fail_skeleton)
        {
            return epidemic::foundation::Result<SkeletonDesc>::Failure(
                epidemic::foundation::Error::Create("animation.skeleton_not_found", "test skeleton missing"));
        }
        return epidemic::foundation::Result<SkeletonDesc>::Success(SkeletonDesc{id, 16});
    }

    epidemic::foundation::Result<AnimationClipDesc> LoadClip(AnimationClipId id) const override
    {
        if (fail_clip)
        {
            return epidemic::foundation::Result<AnimationClipDesc>::Failure(
                epidemic::foundation::Error::Create("animation.clip_not_found", "test clip missing"));
        }
        return epidemic::foundation::Result<AnimationClipDesc>::Success(AnimationClipDesc{id, SkeletonId{1}, 2.0f});
    }
};

struct TestPoseSink final : IAnimationPoseSink
{
    std::vector<std::shared_ptr<const PoseBuffer>> published;
    bool fail = false;

    epidemic::foundation::Result<void> Publish(std::shared_ptr<const PoseBuffer> pose) override
    {
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
    epidemic::foundation::Result<PoseBuffer> EvaluatePose(const AnimationEvaluationRequest& request) const override
    {
        ++evaluations;
        PoseBuffer pose{request.animator, std::vector<epidemic::runtime::Transform>(request.skeleton.joint_count), request.revision};
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
    ok &= ExpectReadiness(runtime, created.Value(), AnimatorReadiness::ResourceMissing, "missing clip should mark resource missing");
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
    bool ok = Expect(!runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{}, 1}).HasValue(), "invalid skeleton should fail");
    ok &= Expect(!runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1}, 0}).HasValue(), "empty skeleton should fail");
    ok &= Expect(runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1}, 1}).HasValue(), "valid skeleton should register");
    ok &= Expect(!runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1}, 1}).HasValue(), "duplicate skeleton should fail");
    ok &= Expect(!runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{}, SkeletonId{1}, AnimationLodLevel::Full}).HasValue(), "invalid owner should fail");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1}, SkeletonId{99}, 1.0f}).HasValue(), "clip with missing skeleton should fail");
    ok &= Expect(runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1}, SkeletonId{1}, 1.0f}).HasValue(), "valid clip should register");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1}, SkeletonId{1}, 1.0f}).HasValue(), "duplicate clip should fail");
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
    ok &= ExpectReadiness(runtime, animator.Value(), AnimatorReadiness::ResourceMissing, "resource failure should mark missing");

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
    return ok && Expect(!tick.HasValue() && tick.GetError().HasCode("animation.publish_failed"),
                        "pose sink failure should propagate from tick");
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

bool TestFactoryProfilesSeparateMockEvaluation()
{
    auto sink = std::make_shared<TestPoseSink>();
    auto resources = std::make_shared<TestResourceSource>();
    auto evaluator = std::make_shared<TestEvaluatorBackend>();
    const auto missing_evaluator = CreateAnimationServices(
        AnimationOptions{.enable_mock_pose_evaluation = true},
        AnimationDependencies{resources, sink});
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
    ok &= TestHandlePlaybackPoseBufferAndFactory();
    ok &= TestPoseQueriesRejectStaleHandle();
    ok &= TestPauseRejectsStoppedAndFinished();
    ok &= TestCrossfadeWithoutCurrentClipStartsTargetWithoutBlend();
    ok &= TestLoopSettingIsolatedPerAnimatorAndClipMetadataImmutable();
    ok &= TestResourceSourceFailureAndPoseSinkPublication();
    ok &= TestFractionalPlaybackRateAccumulates();
    ok &= TestPoseSinkFailurePropagates();
    ok &= TestEventCapacityDropsOldest();
    ok &= TestFactoryProfilesSeparateMockEvaluation();
    return ok ? 0 : 1;
}



