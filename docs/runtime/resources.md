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
- Releasing an invalid handle returns `resource.invalid_handle`.
- Releasing a stale handle returns `resource.stale_handle`.
- Releasing a handle twice returns `resource.reference_underflow`.
- A released ready resource stays cached until explicit eviction or budget pressure evicts unreferenced cache.
- `Evict()` is only valid when the reference count is zero; it releases payload and dependency handles, moves through `Evicting`, then lands in `Evicted` with a new generation.
- `EvictUnreferenced()` and budget eviction must not touch queued, loading, waiting, reloading or already evicting slots.

## Budgets And Failures

`ProcessPendingLoads()` honors item, byte and time budgets. A failed resource increments diagnostics/statistics and does not stop independent queued requests.

Dependency cycles are detected through blocking dependency graph edges, including indirect required chains such as `A -> B -> C -> A`. Failed loads use one rollback path that releases acquired dependency handles, clears pending artifacts, removes graph edges and restores memory accounting.

Required dependencies keep a root resource in `WaitingForDependencies` until they become ready. If a required dependency fails, is evicted or becomes unknown, the root load fails and rolls back.

Optional dependencies do not block root readiness in the baseline policy. If an optional dependency is unavailable, failed, evicted or still loading when the root is ready to commit, the optional handle is released and the root continues.

## How To Use

Request by `ResourceId` plus `ResourceType`, hold returned handles, release them when no longer needed and evict explicitly when desired.

Renderer integrations must acquire and release handles through a composition-owned bridge. Read-only renderer payload queries must not call `Request()` or `ProcessPendingLoads()`.

## Example

```cpp
auto handle = resource_manager.Request({resource_id, resource_type, {}});
auto release = resource_manager.Release(handle.Value());
```

## Testing Strategy

Validate type mismatch rejection, dependency lifetime, explicit eviction behavior, stale/double release failures, required dependency cycles, optional dependency non-blocking behavior, rollback after dependency failure, rollback on memory-budget failure, budgeted queue processing, in-flight eviction protection, immutable payload access and memory statistics for unreferenced cache.
