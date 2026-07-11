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
- `Evicted`

## How To Use

Request by `ResourceId` plus `ResourceType`, hold returned handles, release them when no longer needed and evict explicitly when desired.

## Example

```cpp
auto handle = resource_manager.Request({resource_id, resource_type, {}});
resource_manager.Release(handle.Value());
```

## Testing Strategy

Validate type mismatch rejection, dependency lifetime, explicit eviction behavior and stale-handle state handling.
