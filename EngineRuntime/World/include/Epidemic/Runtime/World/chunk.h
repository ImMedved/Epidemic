#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/World/world_state.h"
#include <cstdint>
#include <optional>

namespace epidemic::runtime
{
struct ChunkDescriptor
{
    ChunkId id{};
    RegionId region_id{};
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;

    [[nodiscard]] constexpr bool operator==(const ChunkDescriptor&) const noexcept = default;
};

class IChunkRegistry
{
  public:
    virtual ~IChunkRegistry() = default;
    [[nodiscard]] virtual foundation::Result<void> RegisterChunk(ChunkDescriptor chunk) = 0;
    [[nodiscard]] virtual std::optional<ChunkDescriptor> FindChunk(ChunkId id) const = 0;
    [[nodiscard]] virtual ChunkState GetChunkState(ChunkId id) const = 0;
};
} 
