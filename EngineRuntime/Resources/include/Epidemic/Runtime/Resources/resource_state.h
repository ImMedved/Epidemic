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

[[nodiscard]] constexpr bool CanTransition(ResourceState from, ResourceState to) noexcept
{
    switch (from)
    {
    case ResourceState::Unknown:
        return to == ResourceState::Unloaded || to == ResourceState::Queued || to == ResourceState::Failed;
    case ResourceState::Unloaded:
        return to == ResourceState::Queued || to == ResourceState::Evicted;
    case ResourceState::Queued:
        return to == ResourceState::Loading || to == ResourceState::Evicted || to == ResourceState::Failed;
    case ResourceState::Loading:
        return to == ResourceState::WaitingForDependencies || to == ResourceState::Ready || to == ResourceState::Failed;
    case ResourceState::WaitingForDependencies:
        return to == ResourceState::Queued || to == ResourceState::Ready || to == ResourceState::Failed;
    case ResourceState::Ready:
        return to == ResourceState::Reloading || to == ResourceState::Evicting;
    case ResourceState::Reloading:
        return to == ResourceState::Loading || to == ResourceState::Ready || to == ResourceState::Failed;
    case ResourceState::Evicting:
        return to == ResourceState::Evicted;
    case ResourceState::Evicted:
        return to == ResourceState::Queued;
    case ResourceState::Failed:
        return to == ResourceState::Queued || to == ResourceState::Evicted;
    }
    return false;
}
} // namespace epidemic::runtime
