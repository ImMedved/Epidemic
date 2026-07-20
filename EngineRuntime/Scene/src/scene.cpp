#include "Epidemic/Runtime/Scene/scene_services.h"

#include "scene_runtime_impl.h"

namespace epidemic::runtime
{
foundation::Result<SceneServices> CreateSceneServices(const SceneOptions& options)
{
    (void)options;

    auto runtime = std::make_shared<SceneRuntime>();
    SceneServices services{};
    services.nodes = runtime;
    services.transforms = runtime;
    services.spatial = runtime;
    services.queries = runtime;
    services.snapshots = runtime;
    return foundation::Result<SceneServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
