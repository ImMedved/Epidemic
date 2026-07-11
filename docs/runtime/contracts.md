# Runtime Contracts

## Core Rules

- No major may command another major directly.
- Cross-major data exchange is allowed only through documented contracts, snapshots, projections, events, command/effect queues or final Support composition.
- Support is created after majors are implemented and tested.

## Dependency Direction

```text
EngineBase/*
  <- RuntimeFoundation
     <- Runtime majors
```

## First-Group Implications

- `RuntimeFoundation` does not depend on other runtime majors.
- `Assets` and `Resources` stay separated.
- `Persistence` does not own `World` or `Serialization` behavior.
- `Environment` consumes external input instead of linking `Time`.
- `Scene` does not know `World`, renderer or physics.
- `World` does not own scene storage, persistence storage or renderer/physics objects.

## Integration Pattern

Future integrations should pass value snapshots such as `TimeSnapshot`, `EnvironmentProjection` or world object snapshots instead of exposing mutable live backdoors between majors.
