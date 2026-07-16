# EngineRuntime

`EngineRuntime` is the modular runtime layer of Epidemic. It provides runtime systems and shared runtime state, but it is not the gameplay framework and not a concrete game.

EngineRuntime provides runtime systems and shared runtime state. GameFramework provides reusable gameplay mechanics. Game provides concrete rules, content and final composition.

## Majors

- `RuntimeFoundation`: runtime ids, states, typed time, budgets and shared spatial vocabulary.
- `Assets`: asset metadata catalog and lookup contracts.
- `Resources`: immutable resource payloads, handles, loader registry, dependency tracking, budgets and cache lifetime management.
- `Serialization`: archive contracts, serializer registry and migrations.
- `Persistence`: persistent records, dirty tracking, tombstones, zone overrides and save transactions.
- `Time`: game time, calendar, pause, scale, skip, snapshots and time events.
- `Environment`: weather, season, climate, surface snapshots and deterministic update input.
- `Scene`: transforms, bounds, scene nodes and spatial queries.
- `World`: regions, chunks, variant object placement, command-based object mutation, residency and materialization boundaries.
- `Streaming`: budgeted chunk residency requests and streaming state transitions.
- `Renderer`: render proxies, views and minimal frame flow contracts.
- `Physics`: bodies, shapes, queries and event/effect boundaries.
- `Navigation`: nav tiles, path queries, traversal costs and obstacle projections.
- `Animation`: skeletons, clips, animator instances, pose state and animation events.
- `Audio`: sound resources, emitters, listeners, mixer placeholders and one-shot events.
- `Simulation`: budgets, scheduled jobs, attention, world memory and effect buffering.
- `Support`: final composition helpers and dependency validation.

## Boundaries

EngineRuntime must not contain concrete game rules, content-specific behavior or final product composition. Runtime majors communicate through ids, snapshots, projections, events, command/effect queues or the Support composition layer.

## Reading Order

1. [layers.md](layers.md)
2. [contracts.md](contracts.md)
3. [modules.md](modules.md)
4. [dependencies.md](dependencies.md)
5. [threading.md](threading.md)
6. [update_order.md](update_order.md)
7. [ownership.md](ownership.md)
8. [error_model.md](error_model.md)
9. [api_stability.md](api_stability.md)
10. [using_runtime.md](using_runtime.md)
11. [architecture_philosophy.md](architecture_philosophy.md)
12. [renderer.md](renderer.md)
