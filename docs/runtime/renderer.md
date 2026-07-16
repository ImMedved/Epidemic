# Renderer

## Purpose

`Renderer` owns render proxies, views and the minimal deterministic frame flow. It consumes resource payloads and transform snapshots through projection interfaces supplied by composition code.

## Public Contracts

- `render_scene.h`: proxy registration, deferred destruction, visibility and dirty flags.
- `view_system.h`: view lifecycle and main view selection.
- `render_resource_bridge.h`: resource lease and payload projection interface.
- `renderer_runtime.h`: `PrepareFrame()`, `RenderFrame()` and frame state.

## Rules

- `IRenderResourceBridge::AcquirePayloads()` acquires resource leases for a proxy.
- `IRenderResourceBridge::ReleasePayloads()` releases those leases when the proxy is flushed or the runtime is destroyed.
- `IRenderResourceBridge::GetPayloads()` is a read-only query. It must not request resources, process resource queues or mutate `Resources`.
- Resource queue processing belongs to the owning composition/update phase, not to renderer payload queries.
- `IRenderSceneSource` projects transform snapshots into Renderer by `RenderTransformId`; Renderer must not own or include Scene state.
- Renderer readiness reflects whether acquired payloads are currently available.

## Forbidden Dependencies

Renderer contracts must not depend on concrete backend resources, gameplay state, Scene storage or resource loading policy. Support may provide adapters that connect Renderer to Resources and Scene.

## Testing Strategy

Renderer tests cover proxy lifecycle, dirty flags, view lifecycle, frame flow, missing resources and symmetric resource acquire/release.
