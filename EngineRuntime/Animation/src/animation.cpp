#include "animation_runtime_impl.h"

#include <utility>

namespace epidemic::runtime::animation
{
std::unique_ptr<AnimationRuntime> CreateAnimationRuntime(AnimationOptions options, AnimationDependencies dependencies)
{
    return std::make_unique<AnimationRuntime>(options, std::move(dependencies));
}

foundation::Result<AnimationServices> CreateAnimationServices(AnimationOptions options, AnimationDependencies dependencies)
{
    options.enable_mock_pose_evaluation = false;
    auto runtime = std::make_shared<AnimationRuntime>(options, std::move(dependencies));

    AnimationServices services{};
    services.skeletons = runtime;
    services.clips = runtime;
    services.runtime = runtime;
    services.poses = runtime;
    services.events = runtime;
    return foundation::Result<AnimationServices>::Success(std::move(services));
}

AnimationServices CreateReferenceAnimationServices(AnimationOptions options)
{
    options.enable_mock_pose_evaluation = true;
    auto runtime = std::make_shared<AnimationRuntime>(options);

    AnimationServices services{};
    services.skeletons = runtime;
    services.clips = runtime;
    services.runtime = runtime;
    services.poses = runtime;
    services.events = runtime;
    return services;
}

AnimationServices CreateMockAnimationServices(AnimationOptions options)
{
    return CreateReferenceAnimationServices(options);
}
} // namespace epidemic::runtime::animation
