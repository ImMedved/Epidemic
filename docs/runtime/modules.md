# Runtime Modules

## Summary

- `RuntimeFoundation`: shared runtime ids, states, budgets and helper vocabulary.
- `Assets`: read/write asset catalog contracts and in-memory catalog implementation.
- `Resources`: request/release resource lifecycle, typed handles, loaders and dependency handles.
- `Serialization`: archive read/write contracts, serializer registry and migration registry.
- `Persistence`: persistent object records, dirty tracking, tombstones, lazy rules and save transactions.
- `Time`: authoritative game time runtime with snapshot and event output.
- `Environment`: region and surface environment state with projections and deterministic external update input.
- `Scene`: scene node registry, transform registry, bounds storage and naive spatial queries.
- `World`: region/chunk registry, object placement and materialization boundary.

## Status

All first-group majors build and have dedicated tests.

## Not Included Yet

- `Streaming`
- `Renderer`
- `Physics`
- `Navigation`
- `Animation`
- `Audio`
- `Simulation`
- `Support`
