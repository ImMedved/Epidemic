#include "animation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <memory>
#include <utility>

namespace epidemic::runtime::animation
{
namespace
{
class ReferenceAnimationEvaluator final : public IAnimationEvaluatorBackend
{
public:
    [[nodiscard]] foundation::Result<PoseBuffer> EvaluatePose(const AnimationEvaluationRequest& request) const override
    {
        PoseBuffer pose{request.animator, std::vector<Transform>(request.skeleton.joint_count), request.revision};
        const float source_sample = static_cast<float>(request.source_time.value.count()) / 1000000.0f;
        const float target_sample = static_cast<float>(request.target_time.value.count()) / 1000000.0f;
        const float blended_sample = source_sample * request.source_weight + target_sample * request.target_weight;
        for (std::size_t index = 0; index < pose.bone_transforms.size(); ++index)
        {
            pose.bone_transforms[index].position.x = blended_sample;
            pose.bone_transforms[index].position.y = static_cast<float>(index);
            pose.bone_transforms[index].position.z = request.target_weight;
        }
        return foundation::Result<PoseBuffer>::Success(std::move(pose));
    }
};
} // namespace

foundation::Result<AnimationServices> CreateAnimationServices(AnimationOptions options, AnimationDependencies dependencies)
{
    options.enable_mock_pose_evaluation = false;
    if (dependencies.evaluator == nullptr)
    {
        return foundation::Result<AnimationServices>::Failure(
            foundation::Error::Create("animation.evaluator_missing", "production animation services require an evaluator backend"));
    }
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
    AnimationDependencies dependencies{};
    dependencies.evaluator = std::make_shared<ReferenceAnimationEvaluator>();
    auto runtime = std::make_shared<AnimationRuntime>(options, std::move(dependencies));

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
