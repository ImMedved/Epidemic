# Runtime Layers

## Layer Model

```text
EngineBase
  -> EngineRuntime/RuntimeFoundation
     -> EngineRuntime/<Major>
        -> EngineRuntime/Support
```

## Rules

- `RuntimeFoundation` is the shared runtime vocabulary layer.
- Runtime majors depend on `RuntimeFoundation` and selected public contracts only.
- Direct major-to-major commanding is forbidden.
- Data crosses major boundaries only through ids, snapshots, projections, events, command/effect queues or Support composition.
- `Support` is a composition layer. It knows majors, but majors do not know `Support`.

## Major Layout

```text
RuntimeFoundation
Assets
Resources
Serialization
Persistence
Time
Environment
Scene
World
Streaming
Renderer
Physics
Navigation
Animation
Audio
Simulation
Support
```

## Ownership

Runtime majors own their local runtime state only. They must not secretly mutate another major's state. Cross-major operations should be represented as public contracts or buffered effects and resolved by explicit orchestration.
