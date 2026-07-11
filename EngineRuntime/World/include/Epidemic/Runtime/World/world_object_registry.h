#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/World/world_object.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
class IWorldObjectRegistry
{
  public:
    virtual ~IWorldObjectRegistry() = default;

    [[nodiscard]] virtual foundation::Result<RuntimeObjectId> CreateObject(WorldObjectRecord record) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyObject(RuntimeObjectId id) = 0;
    [[nodiscard]] virtual std::optional<WorldObjectRecord> FindObject(RuntimeObjectId id) const = 0;
    [[nodiscard]] virtual std::vector<WorldObjectRecord> FindObjectsInChunk(ChunkId chunk) const = 0;
    [[nodiscard]] virtual foundation::Result<void> SetPlacement(RuntimeObjectId id, ObjectPlacement placement) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetResidency(RuntimeObjectId id, ResidencyState state) = 0;
};
} // namespace epidemic::runtime
