# Runtime Modules

## Summary

- `RuntimeFoundation`: shared runtime ids, states, budgets and helper vocabulary.
- `Assets`: read/write asset catalog contracts and in-memory catalog implementation.
- `Resources`: request/release resource lifecycle, typed handles, loaders and dependency handles.
- `Serialization`: archive read/write contracts, serializer registry and migration registry.
- `Persistence`: persistent object records, dirty tracking, tombstones, lazy rules and save transactions.
- `Time`: authoritative game time runtime with snapshot and event output.
- `Environment`: region and surface environment state with projections and deterministic external update input.
- `Scene`: scene node registry, transform registry, bounds storage and spatial queries.
- `World`: region/chunk registry, object placement and materialization boundary.
- `Streaming`: deterministic in-memory streaming scheduler and residency state transitions.
- `Renderer`: render scene proxies, view system and mock frame lifecycle.
- `Physics`: shape/body registry, fake deterministic queries and contact event buffer.
- `Navigation`: tile registry, budgeted path queries, cost providers and obstacle projections.
- `Animation`: skeleton/clip registries, animator lifecycle, pose state and event buffer.
- `Audio`: sound registry, emitter/listener systems, event queue and mixer placeholders.
- `Simulation`: budgeted scheduler, attention maps, world-memory TTL and effect buffer.
- `Support`: individual RegisterXxx helpers, default composition preset and dependency validation.

## Status

All EngineRuntime majors build as standalone CMake targets and have dedicated tests. Support is implemented after the majors and contains composition/validation only.

Architecture tests enforce the frozen shape: public headers compile through the architecture target, majors do not depend on `Support`, private `src` headers do not leak through public includes, service factories exist for composed majors, handle-like ids validate generations and externally visible snapshots carry revisions.

## Not In Runtime

- Concrete game rules or content behavior.
- Direct hidden calls between majors.
- A global monolithic world runtime.
- Backend-specific renderer, physics or audio SDK ownership in these foundation passes.
