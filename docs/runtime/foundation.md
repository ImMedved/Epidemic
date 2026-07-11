# RuntimeFoundation

## Purpose

`RuntimeFoundation` is the shared runtime vocabulary layer for `EngineRuntime`.
It defines stable identity types, common runtime states, async operation status, and generic runtime budgets.

The module exists so that `Assets`, `Resources`, `Persistence`, `Environment`, `Scene`, `World`, and future runtime majors use the same language instead of inventing incompatible ids, enums, and status values.

## What Belongs Here

`RuntimeFoundation` contains:

- runtime identity types such as `AssetId`, `ResourceId`, `RuntimeObjectId`, `PersistentObjectId`, `RegionId`, `ChunkId`, `SurfaceId`, and `SimulationZoneId`
- common runtime state enums such as residency, reality level, persistence tier, simulation lod, and async operation status
- generic `RuntimeBudget` value data
- small helper functions that operate only on these generic runtime types
- an optional generic `RuntimeHandle<Tag>` helper for future majors that need local typed handles without reserving semantic names in `RuntimeFoundation`

## Placement Rules

If a type is needed by only one runtime major, it should stay inside that major.

If a type seems to be needed by multiple majors, it should first be reviewed as a possible integration smell before being moved into `RuntimeFoundation`.

`RuntimeFoundation` is for shared runtime vocabulary, not for collecting unrelated convenience types.

## What Does Not Belong Here

`RuntimeFoundation` must not contain:

- scene-specific ids or handles such as `SceneNodeId`
- semantic handles such as `AssetHandle` or `ResourceHandle`
- asset catalog logic
- resource loading or caching
- serialization formats or save/load behavior
- time systems, clocks, or scheduler implementations
- world logic, scene logic, weather logic, renderer logic, physics logic, audio logic, or gameplay logic
- worker threads or frame-blocking waits
- direct calls into renderer, physics, audio, npc, or gameplay systems

## Public Contracts

Current public headers:

- `runtime_ids.h`
- `runtime_states.h`
- `runtime_budget.h`
- `runtime_operation.h`
- `runtime_foundation.h`

`runtime_handles.h` remains an optional generic helper header and is intentionally not part of the umbrella header.

The contracts are intentionally lightweight and header-oriented. They provide data vocabulary, not runtime behavior orchestration.

## Allowed Dependencies

`RuntimeFoundation` currently links only:

- `EpidemicFoundation`

Forbidden direct runtime-major dependencies:

- `EpidemicRuntimeAssets`
- `EpidemicRuntimeResources`
- `EpidemicRuntimeSerialization`
- `EpidemicRuntimePersistence`
- `EpidemicRuntimeTime`
- `EpidemicRuntimeEnvironment`
- `EpidemicRuntimeScene`
- `EpidemicRuntimeWorld`

It also has no dependency on `Time` concepts and does not depend on `Simulation`.

## States And Budget Semantics

`ResidencyState` describes whether runtime data is unloaded, loading, resident, active, sleeping, or unloading.

`ObjectRealityLevel` distinguishes between abstract facts, logical existence, and physical runtime presence. Use helper functions instead of relying on enum ordinals outside tests.

`PersistenceTier` expresses how strongly an object should survive over time, from disposable data to quest-critical state.

`AsyncOperationStatus` describes generic async work without choosing any scheduler implementation.

`RuntimeBudget` describes optional limits for work by time, item count, and bytes. A default-constructed budget is treated as empty.

## Example

```cpp
#include "Epidemic/Runtime/Foundation/runtime_foundation.h"

using namespace epidemic::runtime;

AssetId asset_id = AssetId::FromString("items/potato.itemdef");
RuntimeObjectId object_id{42};
RuntimeBudget budget{std::chrono::microseconds{250}, 8u, 4096u};

if (asset_id.IsValid() && object_id.IsValid() && !budget.IsEmpty())
{
    // Future runtime majors can share these value types without linking each other.
}
```

## Testing Strategy

`RuntimeFoundation` tests currently verify:

- default id invalid state
- id equality
- hash compatibility with `unordered_map`
- compatibility between `runtime_foundation.h` and `Resources/resource_handle.h`
- state enums compile and preserve intended ordering assumptions
- explicit helper semantics for `ObjectRealityLevel`
- async operation helper semantics
- budget default-empty and populated-budget semantics
- target-level protection against links to other runtime majors through CMake validation
