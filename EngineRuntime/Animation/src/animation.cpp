#include "animation_runtime_impl.h"

namespace epidemic::runtime::animation
{
std::unique_ptr<AnimationRuntime> CreateAnimationRuntime(AnimationOptions options)
{
    return std::make_unique<AnimationRuntime>(options);
}
} // namespace epidemic::runtime::animation
