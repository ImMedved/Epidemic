# World

## Purpose

`World` owns logical runtime world structure: regions, chunks, object identity, placement, reality level, residency, persistence tier, materialization boundaries and immutable query results.

## Public Contracts

- `region.h`: region descriptors and registration contract.
- `chunk.h`: chunk descriptors and chunk state contract.
- `world_location.h`: region/chunk address values.
- `object_placement.h`: placement state for world surface, container, equipped/carried, hidden and destroyed objects.
- `world_object.h`: world object records with independent reality, residency, persistence tier and revision.
- `world_invariants.h`: central invariant validation for object placement, reality/residency and structural ownership.
- `world_commands.h`: command payloads with expected revision and before/after snapshots.
- `world_object_registry.h`: object creation, lookup and command-based mutation entry points.
- `object_materialization.h`: materialization/demotion requests.
- `world_query.h`: read-only object queries.
- `world_services.h`: factory and service bundle for regions, chunks, query, writer and materialization.

## Rules

- Region ids and chunk ids must be valid and unique.
- A chunk must reference an already registered region.
- Object queries return snapshots, not mutable references.
- Region/chunk/reality queries are sorted by runtime object id so observable order does not depend on unordered storage iteration.
- Object creation starts at revision `1`; placement, residency, materialization and demotion increment revision.
- Commands reject stale `expected_revision` with `world.revision_conflict`; `expected_revision == 0` is reserved for compatibility adapters that do not enforce a caller revision.
- World-surface placement requires an existing region, an existing chunk, chunk-region ownership and a valid transform.
- Container placement requires an existing live container, a valid slot and no containment cycle.
- Inventory and equipped placement require an existing live owner; equipped placement also requires a valid slot.
- Persistent ids are unique while the object is registered.
- A valid persistent id requires a non-disposable persistence tier.
- `Physical` objects must be resident/active; `DestroyedPlacement` cannot be active or physical.
- Persistent or non-disposable destroyed objects remain as tombstones with `DestroyedPlacement` and `Unloaded` residency.
- `PlayerTouched`, `Protected` and `QuestCritical` objects cannot be downgraded through the promotion command.
- Materialization is a short atomic World commit. Runtime budget belongs to Streaming, not to World materialization.
- Demotion requires a matching `DemotionCommitToken`, created after the collapse/persistence side of demotion is confirmed by integration code.

## Current Shape

Placement is represented as a tagged variant:

- `WorldSurfacePlacement`
- `ContainerPlacement`
- `InventoryPlacement`
- `EquippedPlacement`
- `HiddenPlacement`
- `DestroyedPlacement`

Legacy setter-style methods remain as compatibility adapters, but new mutation paths should use commands so revision checks, invariant validation and before/after snapshots stay explicit.

## Forbidden Dependencies

`World` must not call Physics, Renderer, Streaming, Persistence, gameplay, inventory, combat, quest, trade or NPC behavior directly.

## Testing Strategy

The `World` tests cover region/chunk registration, duplicate rejection, chunk-region validation, object snapshots, placement validation, containment cycles, owner existence, state-combination validation, command revision conflicts, before/after command results, persistence promotion invariants, revision increments, deterministic query order, materialization and demotion confirmation.
