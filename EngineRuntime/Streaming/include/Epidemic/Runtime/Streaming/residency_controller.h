#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

namespace epidemic::runtime::streaming
{
// Residency transition contract used by Streaming to ask an external controller to
// activate, deactivate or unload chunk-owned runtime state.
class IResidencyController
{
  public:
    virtual ~IResidencyController() = default;

    [[nodiscard]] virtual foundation::Result<void> ActivateChunk(ChunkId chunk) = 0;
    [[nodiscard]] virtual foundation::Result<void> DeactivateChunk(ChunkId chunk) = 0;
    [[nodiscard]] virtual foundation::Result<void> UnloadChunk(ChunkId chunk) = 0;
};
} // namespace epidemic::runtime::streaming
