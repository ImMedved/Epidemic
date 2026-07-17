#include "animation_runtime_impl.h"

#include <iostream>
#include <string_view>
#include <type_traits>

using epidemic::runtime::GameDuration;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::animation::AnimationClipDesc;
using epidemic::runtime::animation::AnimationClipId;
using epidemic::runtime::animation::AnimationLodLevel;
using epidemic::runtime::animation::AnimationPlaybackCommand;
using epidemic::runtime::animation::CreateAnimationServices;
using epidemic::runtime::animation::IAnimationPoseSink;
using epidemic::runtime::animation::IAnimationResourceSource;
using epidemic::runtime::animation::AnimationRuntime;
using epidemic::runtime::animation::AnimatorDesc;
using epidemic::runtime::animation::AnimatorState;
using epidemic::runtime::animation::PoseState;
using epidemic::runtime::animation::SkeletonDesc;
using epidemic::runtime::animation::SkeletonId;

namespace
{
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
    ok &= runtime.RegisterClip(AnimationClipDesc{AnimationClipId{10}, SkeletonId{1}, 1.0f, false}).HasValue();
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
    ok &= Expect(!runtime.RegisterClip(AnimationClipDesc{AnimationClipId{1}, SkeletonId{99}, 1.0f, false}).HasValue(), "clip with missing skeleton should fail");
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
    ok &= Expect(runtime.Crossfade(handle.Value(), AnimationClipId{10}, GameDuration{1}).HasValue(), "crossfade should succeed");
    ok &= Expect(runtime.GetState(handle.Value().id) == AnimatorState::Blending, "crossfade should blend");
    ok &= Expect(runtime.Tick(GameDuration{1}, 1) == 1, "delta tick should advance one animator");

    const auto pose = runtime.GetPoseBuffer(handle.Value());
    ok &= Expect(pose.animator == handle.Value(), "pose buffer should preserve handle");
    ok &= Expect(pose.bone_transforms.size() == 32, "pose buffer should contain skeleton bone transforms");
    ok &= Expect(runtime.Stop(handle.Value()).HasValue(), "stop should succeed");

    const auto services = CreateAnimationServices();
    ok &= Expect(services.skeletons != nullptr && services.clips != nullptr && services.runtime != nullptr &&
                     services.poses != nullptr && services.events != nullptr,
                 "animation services should be populated");
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
    return ok ? 0 : 1;
}
