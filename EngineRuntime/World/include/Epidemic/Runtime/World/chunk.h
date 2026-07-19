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

struct ChangeChunkStateCommand
{
    ChunkId chunk{};
    std::uint64_t expected_revision = 0;
    ChunkState state = ChunkState::Unloaded;
};

struct ChunkSnapshot
{
    ChunkDescriptor descriptor{};
    ChunkState state = ChunkState::Unloaded;
    std::uint64_t revision = 0;
};

[[nodiscard]] constexpr bool CanTransition(ChunkState from, ChunkState to) noexcept
{
    if (from == to)
    {
        return true;
    }
    switch (from)
    {
    case ChunkState::Unloaded:
        return to == ChunkState::Loading;
    case ChunkState::Loading:
        return to == ChunkState::Resident || to == ChunkState::Unloaded;
    case ChunkState::Resident:
        return to == ChunkState::Active || to == ChunkState::Sleeping || to == ChunkState::Unloading;
    case ChunkState::Active:
        return to == ChunkState::Resident || to == ChunkState::Sleeping || to == ChunkState::Unloading;
    case ChunkState::Sleeping:
        return to == ChunkState::Active || to == ChunkState::Resident || to == ChunkState::Unloading;
    case ChunkState::Unloading:
        return to == ChunkState::Unloaded;
    }
    return false;
}

class IChunkRegistry
{
  public:
    virtual ~IChunkRegistry() = default;
    [[nodiscard]] virtual foundation::Result<void> RegisterChunk(ChunkDescriptor chunk) = 0;
    [[nodiscard]] virtual std::optional<ChunkDescriptor> FindChunk(ChunkId id) const = 0;
    [[nodiscard]] virtual foundation::Result<ChunkSnapshot> GetChunkSnapshot(ChunkId id) const = 0;
    [[nodiscard]] virtual foundation::Result<void> SetChunkState(ChangeChunkStateCommand command) = 0;
};
} 
