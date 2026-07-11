# RuntimeFoundation

## Purpose

Defines shared runtime vocabulary: ids, generic handles, residency and reality states, persistence tier, async operation status and runtime budgets.

## Includes

- `runtime_ids.h`
- `runtime_handles.h`
- `runtime_states.h`
- `runtime_budget.h`
- `runtime_operation.h`

## Excludes

- Asset catalogs
- Resource loading
- Serialization logic
- Persistence storage
- Scene-specific handles

## Public Contracts

- `AssetId`, `ResourceId`, `RuntimeObjectId`, `PersistentObjectId`, `RegionId`, `ChunkId`, `SurfaceId`, `SimulationZoneId`
- generic `RuntimeHandle<Tag>` helper
- `ResidencyState`, `ObjectRealityLevel`, `PersistenceTier`, `AsyncOperationStatus`, `SimulationLod`
- `RuntimeBudget` and operation helper functions

## Forbidden Dependencies

No dependency on any runtime major.

## States

- residency: `Unloaded` to `Unloading`
- reality: `AbstractFact`, `Logical`, `Physical`
- persistence tier: `Disposable` to `QuestCritical`

## How To Use

Use these ids and enums in public contracts instead of raw integers or strings.

## Example

```cpp
runtime::RuntimeBudget budget{std::chrono::microseconds{500}, 32u, 65536u};
runtime::RuntimeObjectId object_id{42};
```

## Testing Strategy

Validate default-invalid ids, equality, hashing, budget helpers and explicit reality ordering helpers.
