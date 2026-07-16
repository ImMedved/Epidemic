# Resources

## Purpose

Manages typed resource requests, handles, loader lookup, dependency tracking and release/eviction behavior.

## Includes

- `ResourceHandle`
- resource request/result contracts
- loader interfaces and non-owning loader registry
- resource manager with dependency handle tracking

## Excludes

- asset metadata ownership
- persistence storage
- renderer or scene integration

## Public Contracts

- `IResourceManager`
- `IResourceLoader`
- `IResourceLoaderRegistry`
- `ResourceRequest`, `ResourceState`, `ResourceType`
- immutable `std::shared_ptr<const IResourcePayload>` payload access
- `ResourceMemoryStatistics` for ready bytes, cached unreferenced bytes and resource count
- `ResourceServices` and `CreateResourceServices()`

## Forbidden Dependencies

No dependency on `Assets`.

## States

- `Unknown`
- `Unloaded`
- `Queued`
- `Loading`
- `WaitingForDependencies`
- `Ready`
- `Failed`
- `Evicting`
- `Evicted`
- `Reloading`

## Lifetime

- `Request()`/`Acquire` increments the slot reference count.
- `Release()` decrements the reference count and returns a `Result<void>`.
- Releasing a stale handle or releasing a handle twice is an error.
- A released ready resource stays cached until explicit eviction or budget pressure evicts unreferenced cache.
- `Evict()` is only valid when the reference count is zero; it releases payload and dependency handles, moves through `Evicting`, then lands in `Evicted` with a new generation.
- `EvictUnreferenced()` must not touch queued, loading, waiting, reloading or already evicting slots.

## Budgets And Failures

`ProcessPendingLoads()` honors item, byte and time budgets. A failed resource increments diagnostics/statistics and does not stop independent queued requests.

Dependency cycles are detected before committing dependency graph edges, including indirect chains such as `A -> B -> C -> A`. Failed loads roll back acquired dependency handles, pending artifacts, graph edges and memory accounting.

## How To Use

Request by `ResourceId` plus `ResourceType`, hold returned handles, release them when no longer needed and evict explicitly when desired.

Renderer integrations must acquire and release handles through a composition-owned bridge. Read-only renderer payload queries must not call `Request()` or `ProcessPendingLoads()`.

## Example

```cpp
auto handle = resource_manager.Request({resource_id, resource_type, {}});
auto release = resource_manager.Release(handle.Value());
```

## Testing Strategy

Validate type mismatch rejection, dependency lifetime, explicit eviction behavior, stale/double release failures, dependency cycles, rollback on memory-budget failure, budgeted queue processing, immutable payload access and memory statistics for unreferenced cache.
