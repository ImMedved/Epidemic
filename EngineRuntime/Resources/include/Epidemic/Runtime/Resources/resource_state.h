#pragma once

namespace epidemic::runtime
{
enum class ResourceState
{
    Unknown,
    Unloaded,
    Queued,
    Loading,
    WaitingForDependencies,
    Ready,
    Failed,
    Evicting,
    Evicted,
    Reloading,
};
} // namespace epidemic::runtime
