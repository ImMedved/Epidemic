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
Renderer -> RuntimeFoundation
Physics -> RuntimeFoundation
Navigation -> RuntimeFoundation
Animation -> RuntimeFoundation
Audio -> RuntimeFoundation
Simulation -> RuntimeFoundation
Support -> all runtime majors
```

## Review Checklist

- RuntimeFoundation does not depend on other runtime majors.
- RuntimeFoundation owns neutral ids, time points, budgets and spatial math (`Vec3`, `Transform`, `Aabb`, `Sphere`).
- RuntimeFoundation spatial math is the neutral home for transform composition, quaternion helpers and AABB transformation.
- Assets does not depend on Resources.
- Resources does not depend on World, Renderer, Persistence or Simulation.
- Serialization does not depend on Persistence.
- Persistence does not depend on World implementation.
- Time does not depend on Environment, World or Simulation.
- Environment does not command Renderer, Physics, Audio or Simulation.
- Scene does not know content-specific behavior.
- World does not know higher-level rules.
- Streaming does not own World, Resources or Renderer.
- Renderer does not know World internals, Scene storage or Resources loading policy.
- Physics does not write World or Scene state directly.
- Navigation does not make AI decisions.
- Animation does not contain movement or action rules.
- Audio does not contain content-direction logic.
- Simulation does not own authoritative world state and uses foundation time values instead of depending on Time.
- Support knows majors, but majors do not know Support.

## Contract Boundary

When one major needs information from another, prefer public contracts, immutable snapshots, projections, events, command/effect queues or final Support composition. Do not include private `src` headers across major boundaries.

Consumer majors use their own projection ids for external state. For example, Renderer exposes `RenderTransformId`; Support may adapt that id to Scene's `SceneNodeId` when composing a full runtime.

The frozen Support adapter set is Scene -> Renderer, Resources -> Renderer, Scene -> Physics, Resources -> Animation, Animation -> Renderer, Resources -> Audio, Scene -> Audio, Environment -> Audio, Environment -> Navigation, World/Resources/Persistence -> Streaming, Streaming -> World, Time -> Simulation and Simulation -> domain commit target.

Resource payloads cross module boundaries as immutable payload pointers. Support-owned bridges may hold resource leases and adapt those payloads to renderer-facing projections, but renderer code must not drive resource loading or release policy directly.

## CMake Boundary

Every runtime major records `EpidemicRuntimeSupport` as a forbidden dependency in its local CMake file. `Support` is the only runtime target that may link the complete set of majors. Architecture tests scan this rule so future dependency changes fail fast instead of silently creating cycles.
