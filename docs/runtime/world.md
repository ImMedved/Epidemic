# World

## Purpose

Represents world regions, chunks, object placement, residency and materialization boundaries as data and contracts.

## Includes

- region and chunk registries
- world location and chunk states
- object placement model
- world object registry
- materialization and demotion contracts
- world query API
- in-memory `WorldRuntime`

## Excludes

- persistence storage ownership
- scene transform internals
- renderer or physics objects
- gameplay systems such as inventory, combat or quests

## Public Contracts

- `region.h`
- `chunk.h`
- `world_state.h`
- `world_location.h`
- `object_placement.h`
- `world_object.h`
- `world_object_registry.h`
- `object_materialization.h`
- `world_query.h`

## Forbidden Dependencies

No dependency on `Persistence`, `Scene`, `Environment` or `Resources`.

## States

- chunk state: `Unloaded` to `Unloading`
- object reality: `AbstractFact`, `Logical`, `Physical`
- materialization state: `NotMaterialized` to `Failed`

## How To Use

Register regions and chunks, create world objects with placements, update residency and call materialize/demote to move across reality levels.

## Example

```cpp
auto id = world.CreateObject(record);
world.SetPlacement(id.Value(), placement);
world.Materialize({persistent_id, runtime::ObjectRealityLevel::Physical, {}});
```

## Testing Strategy

Validate region/chunk storage, placement updates, world queries and logical-to-physical demotion/materialization behavior.
