# Runtime Layers

## Layer Model

```text
EngineBase
  -> EngineRuntime/RuntimeFoundation
     -> EngineRuntime/<Major>
        -> Support (future composition layer)
```

## Rules

- `RuntimeFoundation` is the only shared runtime vocabulary layer.
- Runtime majors depend on `RuntimeFoundation` and selected `EngineBase` modules only.
- Direct major-to-major commanding is forbidden.
- Data crosses major boundaries only through ids, snapshots, projections, events, queues or a future support layer.

## First Group Layout

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
```

## Support

`Support` is intentionally absent at this stage. Composition must happen only after majors are implemented, tested and documented.
