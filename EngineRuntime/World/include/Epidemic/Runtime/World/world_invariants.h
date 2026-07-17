#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/runtime_states.h"
#include "Epidemic/Runtime/World/chunk.h"
#include "Epidemic/Runtime/World/region.h"
#include "Epidemic/Runtime/World/world_query.h"

namespace epidemic::runtime
{
[[nodiscard]] foundation::Result<void> ValidateRealityResidencyCombination(ObjectRealityLevel reality, ResidencyState residency);

[[nodiscard]] foundation::Result<void> ValidateWorldObjectInvariant(
    const WorldObjectSnapshot& object,
    const IRegionRegistry& regions,
    const IChunkRegistry& chunks,
    const IWorldQuery& objects);
} // namespace epidemic::runtime
