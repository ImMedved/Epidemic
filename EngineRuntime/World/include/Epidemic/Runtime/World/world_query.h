#pragma once

#include "Epidemic/Runtime/World/world_object.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
class IWorldQuery
{
  public:
    virtual ~IWorldQuery() = default;

    [[nodiscard]] virtual std::optional<WorldObjectRecord> FindObject(RuntimeObjectId id) const = 0;
    [[nodiscard]] virtual std::vector<WorldObjectRecord> FindObjectsInRegion(RegionId region) const = 0;
    [[nodiscard]] virtual std::vector<WorldObjectRecord> FindObjectsInChunk(ChunkId chunk) const = 0;
    [[nodiscard]] virtual std::vector<WorldObjectRecord> FindObjectsByReality(ObjectRealityLevel reality) const = 0;
};
} // namespace epidemic::runtime
