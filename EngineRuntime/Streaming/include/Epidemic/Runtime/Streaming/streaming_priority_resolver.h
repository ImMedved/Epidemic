#pragma once

#include "Epidemic/Runtime/Streaming/streaming_types.h"

namespace epidemic::runtime::streaming
{
// Optional policy interface that lets integrations derive request priority from external
// context such as player position, camera focus or region importance.
class IStreamingPriorityResolver
{
  public:
    virtual ~IStreamingPriorityResolver() = default;

    [[nodiscard]] virtual StreamingPriorityClass ResolvePriority(ChunkId chunk) const = 0;
};
} // namespace epidemic::runtime::streaming
