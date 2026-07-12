#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Streaming/streaming_types.h"

#include <optional>

namespace epidemic::runtime::streaming
{
// File note:
// Adapter interfaces that let Streaming request chunk-related data from other majors
// through stable contracts, while still being able to run in mock mode when absent.
class IStreamingWorldSource
{
  public:
    virtual ~IStreamingWorldSource() = default;

    [[nodiscard]] virtual std::optional<RegionId> ResolveRegion(ChunkId chunk) const = 0;
};

class IStreamingPersistenceSource
{
  public:
    virtual ~IStreamingPersistenceSource() = default;

    [[nodiscard]] virtual foundation::Result<void> PrepareChunkData(const StreamingRequest& request) = 0;
};

class IStreamingResourceSource
{
  public:
    virtual ~IStreamingResourceSource() = default;

    [[nodiscard]] virtual foundation::Result<void> PrepareChunkResources(const StreamingRequest& request) = 0;
    [[nodiscard]] virtual foundation::Result<void> ReleaseChunkResources(ChunkId chunk) = 0;
};
} // namespace epidemic::runtime::streaming
