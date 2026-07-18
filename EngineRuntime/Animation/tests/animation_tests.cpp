#include "animation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

using epidemic::runtime::GameDuration;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::animation::AnimationClipDesc;
using epidemic::runtime::animation::AnimationClipId;
using epidemic::runtime::animation::AnimationLodLevel;
using epidemic::runtime::animation::AnimationOptions;
using epidemic::runtime::animation::AnimationPlaybackCommand;
using epidemic::runtime::animation::AnimationDependencies;
using epidemic::runtime::animation::CreateAnimationServices;
using epidemic::runtime::animation::CreateMockAnimationServices;
using epidemic::runtime::animation::CreateReferenceAnimationServices;
using epidemic::runtime::animation::IAnimationPoseSink;
using epidemic::runtime::animation::IAnimationResourceSource;
using epidemic::runtime::animation::AnimationRuntime;
using epidemic::runtime::animation::AnimatorDesc;
using epidemic::runtime::animation::AnimatorState;
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

    epidemic::foundation::Result<void> Publish(std::shared_ptr<const PoseBuffer> pose) override
    {
        published.push_back(std::move(pose));
        return epidemic::foundation::Result<void>::Success();
    }
};

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
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
    AnimationRuntime runtime{{}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto created = runtime.CreateAnimator(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(created.HasValue(), "valid animator should be created");
    ok &= Expect(runtime.GetState(created.Value()) == AnimatorState::Ready, "new animator should be ready");
    ok &= Expect(runtime.DestroyAnimator(created.Value()).HasValue(), "created animator should destroy");
    ok &= Expect(runtime.GetState(created.Value()) == AnimatorState::Disabled, "destroyed animator should read disabled");
    return ok;
}

bool TestPlayInvalidClipReturnsError()
{
    AnimationRuntime runtime{{}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto created = runtime.CreateAnimator(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(created.HasValue(), "valid animator should be created");
    ok &= Expect(!runtime.Play(created.Value(), AnimationClipId{999}).HasValue(), "missing clip should fail playback");
    ok &= Expect(runtime.GetState(created.Value()) == AnimatorState::ResourceMissing, "missing clip should mark resource missing");
    return ok;
}

bool TestStateTransitionsAndEvents()
{
    AnimationRuntime runtime{{}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto created = runtime.CreateAnimator(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(created.HasValue(), "valid animator should be created");
    ok &= Expect(runtime.Play(created.Value(), AnimationClipId{10}).HasValue(), "registered clip should play");
    ok &= Expect(runtime.GetState(created.Value()) == AnimatorState::Playing, "animator should be playing");
    ok &= Expect(runtime.GetPoseSnapshot(created.Value()).revision == 2, "play should revise pose snapshot");
    ok &= Expect(runtime.Events().size() == 1, "play should queue started event");
    ok &= Expect(runtime.Tick(1) == 1, "tick should advance playing animator");
    ok &= Expect(runtime.GetState(created.Value()) == AnimatorState::Finished, "non-looping mock clip should finish after tick");
    ok &= Expect(runtime.GetPoseState(created.Value()) == PoseState::Ready, "pose should become ready after mock evaluation");
    ok &= Expect(runtime.GetPoseSnapshot(created.Value()).revision == 3, "tick should revise pose snapshot");
    ok &= Expect(runtime.Events().size() == 2, "finish should queue second event");
    runtime.Clear();
    ok &= Expect(runtime.Events().empty(), "event buffer should clear");
    return ok;
}

bool TestTickOrderIsDeterministic()
{
    AnimationRuntime runtime{{}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto first = runtime.CreateAnimator(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    const auto second = runtime.CreateAnimator(AnimatorDesc{RuntimeObjectId{78}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(first.HasValue() && second.HasValue(), "animators should be created");
    ok &= Expect(runtime.Play(first.Value(), AnimationClipId{10}).HasValue(), "first should play");
    ok &= Expect(runtime.Play(second.Value(), AnimationClipId{10}).HasValue(), "second should play");
    ok &= Expect(runtime.Tick(1) == 1, "budget should advance one animator");
    ok &= Expect(runtime.GetState(first.Value()) == AnimatorState::Finished, "first id should advance first");
    ok &= Expect(runtime.GetState(second.Value()) == AnimatorState::Playing, "second id should wait");
    return ok;
}

bool TestLodPlaceholderChangesPoseState()
{
    AnimationRuntime runtime{{}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto created = runtime.CreateAnimator(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(created.HasValue(), "valid animator should be created");
    ok &= Expect(runtime.SetLod(created.Value(), AnimationLodLevel::Frozen).HasValue(), "lod update should succeed");
    ok &= Expect(runtime.GetPoseState(created.Value()) == PoseState::Clean, "frozen LOD should keep pose clean");
    return ok;
}

bool TestValidationFailures()
{
    AnimationRuntime runtime{{}};
    bool ok = Expect(!runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{}, 1}).HasValue(), "invalid skeleton should fail");
    ok &= Expect(!runtime.RegisterSkeleton(SkeletonDesc{SkeletonId{1}, 0}).HasValue(), "empty skeleton should fail");
    ok &= Expect(!runtime.CreateAnimator(AnimatorDesc{RuntimeObjectId{}, SkeletonId{1}, AnimationLodLevel::Full}).HasValue(), "invalid owner should fail");
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1}, SkeletonId{99}, 1.0f}).HasValue(), "clip with missing skeleton should fail");
    return ok;
}

bool TestHandlePlaybackPoseBufferAndFactory()
{
    AnimationRuntime runtime{{}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto handle = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(handle.HasValue(), "handle animator should be created");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{handle.Value(), AnimationClipId{10}, false, GameDuration{0}}).HasValue(), "handle play should succeed");
    ok &= Expect(runtime.Pause(handle.Value()).HasValue(), "pause should succeed");
    ok &= Expect(runtime.GetState(handle.Value().id) == AnimatorState::Paused, "pause should update state");
    ok &= Expect(runtime.Crossfade(handle.Value(), AnimationClipId{11}, GameDuration{2}).HasValue(), "crossfade should succeed");
    ok &= Expect(runtime.GetState(handle.Value().id) == AnimatorState::Blending, "crossfade should blend");
    ok &= Expect(runtime.Tick(GameDuration{1}, 1) == 1, "delta tick should advance one animator");

    const auto pose = runtime.GetPoseBuffer(handle.Value());
    ok &= Expect(pose.animator == handle.Value(), "pose buffer should preserve handle");
    ok &= Expect(pose.bone_transforms.size() == 32, "pose buffer should contain skeleton bone transforms");
    ok &= Expect(!pose.bone_transforms.empty() && pose.bone_transforms.front().position.z > 0.49f &&
                     pose.bone_transforms.front().position.z < 0.51f,
                 "crossfade should publish halfway target weight");
    ok &= Expect(runtime.Tick(GameDuration{1}, 1) == 1, "second delta tick should complete crossfade");
    ok &= Expect(runtime.GetState(handle.Value().id) == AnimatorState::Playing, "completed crossfade should continue target playback");
    ok &= Expect(runtime.Stop(handle.Value()).HasValue(), "stop should succeed");

    const auto services = CreateAnimationServices();
    ok &= Expect(services.HasValue(), "production animation services should be created");
    ok &= Expect(services.HasValue() && services.Value().skeletons != nullptr && services.Value().clips != nullptr &&
                     services.Value().runtime != nullptr && services.Value().poses != nullptr && services.Value().events != nullptr,
                 "animation services should be populated");
    return ok;
}

bool TestLoopSettingIsolatedPerAnimatorAndClipMetadataImmutable()
{
    AnimationRuntime runtime{{}};
    bool ok = Expect(SeedResources(runtime), "resources should seed");
    const auto first = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    const auto second = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{78}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(first.HasValue() && second.HasValue(), "animators should be created");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{first.Value(), AnimationClipId{10}, true, GameDuration{}}).HasValue(),
                 "first animator should play looping instance");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{second.Value(), AnimationClipId{10}, false, GameDuration{}}).HasValue(),
                 "second animator should play non-looping instance");
    ok &= Expect(runtime.Tick(GameDuration{1}, 2) == 2, "both animators should tick");
    ok &= Expect(runtime.GetState(first.Value().id) == AnimatorState::Playing, "looping instance should keep playing");
    ok &= Expect(runtime.GetState(second.Value().id) == AnimatorState::Finished, "non-looping instance should finish");

    const auto third = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{79}, SkeletonId{1}, AnimationLodLevel::Full});
    ok &= Expect(third.HasValue(), "third animator should be created");
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{third.Value(), AnimationClipId{10}, false, GameDuration{}}).HasValue(),
                 "clip metadata should not have been mutated to loop");
    ok &= Expect(runtime.Tick(GameDuration{1}, 3) >= 1, "third animator should tick");
    ok &= Expect(runtime.GetState(third.Value().id) == AnimatorState::Finished, "third non-looping instance should finish");
    return ok;
}

bool TestResourceSourceFailureAndPoseSinkPublication()
{
    auto resources = std::make_shared<TestResourceSource>();
    auto sink = std::make_shared<TestPoseSink>();
    AnimationRuntime runtime{AnimationOptions{.enable_mock_pose_evaluation = true}, AnimationDependencies{resources, sink}};

    const auto animator = runtime.CreateAnimatorHandle(AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    bool ok = Expect(animator.HasValue(), "resource-backed skeleton should create animator");
    resources->fail_clip = true;
    ok &= Expect(!runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}, false, GameDuration{}}).HasValue(),
                 "resource source clip failure should propagate");
    ok &= Expect(runtime.GetState(animator.Value().id) == AnimatorState::ResourceMissing, "resource failure should mark missing");

    resources->fail_clip = false;
    ok &= Expect(runtime.Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}, false, GameDuration{}}).HasValue(),
                 "resource-backed clip should play after failure clears");
    ok &= Expect(runtime.Tick(GameDuration{1}, 1) == 1, "resource-backed animator should tick");
    ok &= Expect(!sink->published.empty(), "pose sink should receive immutable pose buffer");
    ok &= Expect(sink->published.back() != nullptr && sink->published.back()->bone_transforms.size() == 16,
                 "published pose should use resource-backed skeleton");
    static_assert(std::is_same_v<decltype(sink->published.back()), std::shared_ptr<const PoseBuffer>&>);
    return ok;
}

bool TestFactoryProfilesSeparateMockEvaluation()
{
    auto sink = std::make_shared<TestPoseSink>();
    auto resources = std::make_shared<TestResourceSource>();
    const auto production = CreateAnimationServices(
        AnimationOptions{.enable_mock_pose_evaluation = true},
        AnimationDependencies{resources, sink});
    if (!Expect(production.HasValue(), "production animation factory should succeed"))
    {
        return false;
    }

    const auto animator = production.Value().runtime->CreateAnimatorHandle(
        AnimatorDesc{RuntimeObjectId{77}, SkeletonId{1}, AnimationLodLevel::Full});
    bool ok = Expect(animator.HasValue(), "production resource-backed animator should create");
    ok &= Expect(production.Value().runtime->Play(AnimationPlaybackCommand{animator.Value(), AnimationClipId{10}, false, GameDuration{}}).HasValue(),
                 "production animator should play");
    ok &= Expect(production.Value().runtime->Tick(GameDuration{1}, 1) == 1, "production animator should tick");
    ok &= Expect(sink->published.empty(), "production factory should not enable mock pose publication");

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

    bool ok = true;
    ok &= TestCreateDestroyAnimator();
    ok &= TestPlayInvalidClipReturnsError();
    ok &= TestStateTransitionsAndEvents();
    ok &= TestTickOrderIsDeterministic();
    ok &= TestLodPlaceholderChangesPoseState();
    ok &= TestValidationFailures();
    ok &= TestHandlePlaybackPoseBufferAndFactory();
    ok &= TestLoopSettingIsolatedPerAnimatorAndClipMetadataImmutable();
    ok &= TestResourceSourceFailureAndPoseSinkPublication();
    ok &= TestFactoryProfilesSeparateMockEvaluation();
    return ok ? 0 : 1;
}
