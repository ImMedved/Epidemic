# Runtime Dependencies

## Direction

`Support` may depend on every runtime major because it composes them. No runtime major may depend on `Support`.

```text
RuntimeFoundation -> no runtime major
Assets -> RuntimeFoundation
Resources -> RuntimeFoundation
Serialization -> RuntimeFoundation
Persistence -> RuntimeFoundation
Time -> RuntimeFoundation
Environment -> RuntimeFoundation
Scene -> RuntimeFoundation
World -> RuntimeFoundation
Streaming -> RuntimeFoundation
Renderer -> RuntimeFoundation, Scene, Resources
Physics -> RuntimeFoundation, Scene
Navigation -> RuntimeFoundation, Scene
Animation -> RuntimeFoundation
Audio -> RuntimeFoundation, Scene
Simulation -> RuntimeFoundation, Time
Support -> all runtime majors
```

## Review Checklist

- RuntimeFoundation does not depend on other runtime majors.
- Assets does not depend on Resources.
- Resources does not depend on World, Renderer, Persistence or Simulation.
- Serialization does not depend on Persistence.
- Persistence does not depend on World implementation.
- Time does not depend on Environment, World or Simulation.
- Environment does not command Renderer, Physics, Audio or Simulation.
- Scene does not know content-specific behavior.
- World does not know higher-level rules.
- Streaming does not own World, Resources or Renderer.
- Renderer does not know World internals.
- Physics does not write World state directly.
- Navigation does not make AI decisions.
- Animation does not contain movement or action rules.
- Audio does not contain content-direction logic.
- Simulation does not own authoritative world state.
- Support knows majors, but majors do not know Support.

## Contract Boundary

When one major needs information from another, prefer public contracts, immutable snapshots, projections, events, command/effect queues or final Support composition. Do not include private `src` headers across major boundaries.
