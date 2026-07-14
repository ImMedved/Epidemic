# Runtime Architecture Philosophy

EngineRuntime is a collection of runtime majors, not a monolith. Each major owns a narrow slice of runtime state and exposes public contracts for other layers to observe or compose.

## Principles

- Keep shared vocabulary in `RuntimeFoundation`.
- Keep major ownership local and explicit.
- Prefer snapshots, projections, events and queues over direct mutation across modules.
- Keep Support as composition, not behavior.
- Keep concrete content and product rules outside EngineRuntime.

## Why This Shape

The engine wants long-lived systemic worlds without collapsing everything into a single god object. The runtime layer provides stable infrastructure for residency, rendering, physics, navigation, animation, audio and simulation while leaving higher-level interpretation to later layers.

## Future Work

Future passes can replace mock/in-memory implementations with backend integrations or richer runtime internals as long as the public contracts and dependency direction remain intact.
