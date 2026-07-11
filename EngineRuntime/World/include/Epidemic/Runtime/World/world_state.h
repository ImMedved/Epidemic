#pragma once

namespace epidemic::runtime
{
enum class ChunkState
{
    Unloaded,
    Loading,
    Resident,
    Active,
    Sleeping,
    Unloading,
};
} // namespace epidemic::runtime
