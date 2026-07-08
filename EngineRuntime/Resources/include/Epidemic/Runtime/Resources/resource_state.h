#pragma once

namespace epidemic::runtime
{
enum class ResourceState
{
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
