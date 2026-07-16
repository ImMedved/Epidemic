#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/World/chunk.h"
#include "Epidemic/Runtime/World/object_materialization.h"
#include "Epidemic/Runtime/World/region.h"
#include "Epidemic/Runtime/World/world_object_registry.h"
#include "Epidemic/Runtime/World/world_query.h"

#include <memory>

namespace epidemic::runtime
{
struct WorldOptions
{
};

using IWorldObjectQuery = IWorldQuery;
using IWorldObjectWriter = IWorldObjectRegistry;
using IWorldMaterializationRuntime = IObjectMaterializer;

struct WorldServices
{
    std::shared_ptr<IRegionRegistry> regions;
    std::shared_ptr<IChunkRegistry> chunks;
    std::shared_ptr<IWorldObjectQuery> query;
    std::shared_ptr<IWorldObjectWriter> writer;
    std::shared_ptr<IWorldMaterializationRuntime> materialization;
};

[[nodiscard]] foundation::Result<WorldServices> CreateWorldServices(const WorldOptions& options = {});
} // namespace epidemic::runtime
