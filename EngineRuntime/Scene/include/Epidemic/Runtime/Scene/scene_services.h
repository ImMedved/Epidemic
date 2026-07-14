#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Scene/scene_node_registry.h"
#include "Epidemic/Runtime/Scene/scene_query.h"
#include "Epidemic/Runtime/Scene/spatial_index.h"
#include "Epidemic/Runtime/Scene/transform_registry.h"

#include <memory>

namespace epidemic::runtime
{
struct SceneOptions
{
};

struct SceneServices
{
    std::shared_ptr<ISceneNodeRegistry> nodes;
    std::shared_ptr<ITransformRegistry> transforms;
    std::shared_ptr<ISpatialIndex> spatial;
    std::shared_ptr<ISceneQuery> queries;
    std::shared_ptr<ISceneSnapshotProvider> snapshots;
};

[[nodiscard]] foundation::Result<SceneServices> CreateSceneServices(const SceneOptions& options = {});
} // namespace epidemic::runtime
