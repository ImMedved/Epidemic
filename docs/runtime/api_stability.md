# Runtime API Stability

EngineRuntime currently promises source-level API stability for the frozen public runtime contracts. ABI stability is not declared.

## Frozen Public Headers

The frozen source surface is the public header tree under `EngineRuntime/*/include/Epidemic/Runtime/**` for these majors:

- `RuntimeFoundation`
- `Assets`
- `Resources`
- `Serialization`
- `Persistence`
- `Time`
- `Environment`
- `Scene`
- `World`
- `Streaming`
- `Renderer`
- `Physics`
- `Navigation`
- `Animation`
- `Audio`
- `Simulation`
- `Support`

Public headers must compile without private cross-major includes. Headers from another major's `src` directory and private implementation headers are not part of the stable API.

## Stable Types

Stable public types include:

- foundation ids, typed time values, budgets and spatial math types;
- resource, asset, scene, world, streaming, renderer, physics, navigation, animation, audio, simulation, persistence and environment ids;
- generation-validated handles and documented never-reuse ids;
- immutable snapshots with revision fields for externally visible state;
- command, event, proposal and effect records used at module boundaries;
- world invariant helpers, placement variants and demotion confirmation tokens;
- `XxxServices` bundles returned by public factories;
- `RuntimeSupportOptions`, `RuntimeSupportRegistration` and Support validation results.

Callers may rely on public field meaning and state-transition semantics. They must not rely on private container layout, concrete in-memory implementation classes or diagnostic wording.

## Stable Factories

Public factories are stable entry points for runtime composition:

- `CreateAssetServices`
- `CreateResourceServices`
- `CreateSerializationServices`
- `CreatePersistenceServices`
- `CreateTimeServices`
- `CreateEnvironmentServices`
- `CreateSceneServices`
- `CreateWorldServices`
- `streaming::CreateStreamingServices`
- `renderer::CreateRendererServices`
- `renderer::CreateMockRendererServices`
- `physics::CreatePhysicsServices`
- `navigation::CreateMockNavigationServices`
- `animation::CreateAnimationServices`
- `audio::CreateMockAudioServices`
- `simulation::CreateSimulationServices`
- individual `RegisterXxx` helpers in Support
- `RegisterDefaultEngineRuntime`

Production factories and explicit mock factories stay separated. `RegisterDefaultEngineRuntime` must not silently enable mock behavior outside its documented preset.

## Backend Extension Ports

Backend and integration extension points are public contracts, not private side calls:

- Serialization archives, serializer registry and migration registry.
- Persistence storage, save transactions, conflict detection and tombstone handling.
- Persistence durability policy, backend save/flush ports and atomic candidate-snapshot commit semantics.
- Streaming data source, priority policy, residency proposals and commit sink.
- Renderer resource bridge, render-scene projections, view data and frame lifecycle.
- Physics generation handles, body snapshots, backend body/shape ports, event buffer, transform source/sink projections and effect sink.
- Navigation tile source, cost provider, obstacle projections and budgeted query flow.
- Animation resource source, animator registry, pose snapshots and pose/event sinks.
- Audio backend, resource source, listener/emitter transforms and one-shot event queue.
- Simulation deterministic jobs, attention/relevance inputs, world-memory store and effect buffer.
- Environment deterministic external input, region/surface state and query projections.

Support may connect these ports through allowed adapters listed in `update_order.md`. Runtime majors must not include or call another major's private implementation to reach across a boundary.

## Explicitly Unstable Details

The following are implementation details and may change without API migration:

- files under each major's `src` directory;
- private implementation headers and classes;
- concrete in-memory/mock backend data structures;
- exact allocation strategy, cache eviction internals and container choices;
- diagnostic message text;
- non-contractual ordering of internal maps or queues;
- performance characteristics of placeholder/mock implementations;
- build directory names and local test harness layout.

## Save And Schema Guarantees

Save-facing public contracts are versioned at the schema boundary. Serialization and persistence changes that affect saved data must provide one of:

- a migration path through the public migration registry;
- a schema-version bump with documented compatibility limits;
- explicit rejection with a stable error code when old data cannot be loaded.

Persistence transactions remain atomic from the public caller's point of view: a failed save transaction must not publish partial state as a successful save. Conflict and tombstone semantics are part of the public persistence contract.

When `PersistenceDurability` requires backend writes, `Save()` and optional `Flush()` complete before the candidate snapshot is published in memory. Backend failure leaves the previous in-memory snapshot and revision unchanged.

## Change Rules After Freeze

Breaking public API changes require:

- an updated entry in this document;
- migration notes for callers and save/schema data when relevant;
- focused tests that cover old/new state transition behavior;
- updated architecture tests when dependency, factory, handle or snapshot rules change;
- review that the change does not introduce private cross-major includes or major-to-`Support` dependencies.
