# EngineRuntime First Group

`EngineRuntime` is the modular runtime layer of Epidemic. The first group defines shared vocabulary and the first standalone runtime majors without turning the engine into a monolith.

## Modules

- `RuntimeFoundation`: runtime ids, states, budgets and shared vocabulary.
- `Assets`: asset metadata catalog and lookup contracts.
- `Resources`: resource handles, loader registry, dependency tracking and lifetime management.
- `Serialization`: archive contracts, serializer registry and migrations.
- `Persistence`: persistent records, dirty tracking, tombstones, zone overrides and save transactions.
- `Time`: game time, calendar, pause, scale, skip, snapshots and time events.
- `Environment`: weather, season, climate, surface snapshots and deterministic update input.
- `Scene`: transforms, bounds, scene nodes and naive spatial queries.
- `World`: regions, chunks, object placement, residency and materialization boundaries.

## Architectural Goal

The first group gives stable contracts for future runtime majors. It does not provide renderer behavior, physics bodies, NPC logic, gameplay rules, procedural weather simulation or final integration wiring.

## Reading Order

1. [layers.md](layers.md)
2. [contracts.md](contracts.md)
3. [modules.md](modules.md)
4. Module pages for detailed contracts.
