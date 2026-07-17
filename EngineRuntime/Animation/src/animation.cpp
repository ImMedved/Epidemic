#include "animation_runtime_impl.h"

namespace epidemic::runtime::animation
{
std::unique_ptr<AnimationRuntime> CreateAnimationRuntime(AnimationOptions options)
{
    return std::make_unique<AnimationRuntime>(options);
}

AnimationServices CreateAnimationServices(AnimationOptions options)
{
    auto runtime = std::make_shared<AnimationRuntime>(options);

    AnimationServices services{};
    services.skeletons = runtime;
    services.clips = runtime;
    services.runtime = runtime;
    services.poses = runtime;
    services.events = runtime;
    return services;
}
} // namespace epidemic::runtime::animation
