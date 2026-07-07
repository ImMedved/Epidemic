# EngineBase Modules

`EngineBase` is organized as separate CMake targets. Each module owns a small contract surface and has a clear dependency position.

## Dependency Shape

```text
Foundation
  <- Memory
  <- Diagnostics
  <- Core
  <- Platform
  <- Input
  <- RHI
  <- RHI_D3D11
  <- Support
  <- Apps / Tests
```

`Support` is composition glue. It is allowed to depend on lower modules, but no lower module may depend on it.

## Foundation

Target: `EpidemicFoundation`

Purpose: base primitives used by all other layers.

Public contracts include `Error`, `Result<T>`, ids, handles, path helpers, and time wrappers.

Rules: no dependency on other EngineBase modules. Do not add engine systems here.

## Memory

Target: `EpidemicMemory`

Purpose: observable memory baseline.

Public contracts include allocation tags, allocator helpers, `IMemoryTracker`, `MemoryTracker`, memory statistics, and budget checks.

Rules: Memory must stay independent from Core, Diagnostics, Platform, Input, RHI, and Support. Runtime integration is done by Support registering `IMemoryTracker`.

## Diagnostics

Target: `EpidemicDiagnostics`

Purpose: logging, counters, profiling scopes, and thread naming.

Public contracts include `ILogger`, `ConsoleLogger`, `CounterRegistry`, profiling scopes, and thread context helpers.

Rules: diagnostics must remain safe to disable or keep minimal. Do not add gameplay telemetry here.

## Core

Target: `EpidemicCore`

Purpose: microkernel/runtime orchestration.

Public contracts include `Application`, `ServiceContainer`, `ModuleRegistry`, `IEventBus`, `ITaskScheduler`, `IMainThreadDispatcher`, configuration service, frame context, and frame phases.

Rules: Core does not know about Platform, Input, RHI, D3D11, renderer, resources, world, or gameplay.

## Platform

Target: `EpidemicPlatform`

Purpose: Windows runtime and native windowing.

Public contracts include `IPlatformRuntime`, `IWindowSystem`, `IWindow`, `NativeWindowHandle`, platform events, dynamic library loading, and `WindowsPlatformRuntime`.

Rules: Platform exposes OS events upward. It must not know about gameplay, renderer, or input actions.

## Input

Target: `EpidemicInput`

Purpose: platform-neutral input state and snapshots.

Public contracts include `IInputSystem`, input events, keyboard state, mouse state, key codes, mouse buttons, and per-frame input snapshots.

Rules: Input stops at raw input meaning. Gameplay bindings such as Attack, Interact, Talk, or OpenInventory belong in upper layers.

## RHI

Target: `EpidemicRHI`

Purpose: minimal presentation GPU boundary.

Public contracts include `IRhiDevice`, `IRhiCommandContext`, `IRhiSwapChain`, presentation descriptors, pixel formats, clear, present, and resize.

Rules: RHI is not a renderer. It must not contain buffers, textures, materials, meshes, scene, camera, lights, UI, assets, or gameplay concepts at this stage.

## RHI_D3D11

Target: `EpidemicRHI_D3D11`

Purpose: Windows D3D11 backend behind the RHI contracts.

Public contract: `CreateD3D11RhiDevice`.

Rules: D3D11 details stay inside this backend. RHI must not depend on RHI_D3D11.

## Support

Target: `EpidemicEngineBaseSupport`

Purpose: official composition helper layer for apps, tests, and future upper executables.

Public contracts include `RegisterEngineBase`, `RegisterWindowsRuntime`, `RegisterInputRuntime`, `RegisterGraphicsRuntime`, `CreateMainWindow`, `RegisterMainSwapChain`, and frame-loop helper registration.

Rules: Support may wire modules together, but it must not become a new engine system. Put reusable runtime systems in `EngineRuntime`, not in Support.

## Apps

Targets:
- `EpidemicHeadlessCoreApp`
- `EpidemicWindowSmokeApp`
- `EpidemicInputSmokeApp`
- `EpidemicRhiClearScreenApp`

Purpose: smoke validation of real runtime behavior.

Rules: apps should stay thin. If an app repeats generic wiring, move that wiring into Support.

## Tests

Targets are split into unit, integration, and regression tests.

Purpose: protect module contracts, integration seams, and freeze rules.

Rules: tests should not link unnecessary modules if that would hide dependency violations.
