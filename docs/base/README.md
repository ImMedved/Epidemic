# EngineBase Detailed Overview

`EngineBase` is the stable low-level runtime slice of the engine. Its job is to provide a small, testable, composition-friendly foundation that future layers can build on without constantly changing the core contracts.

It is intentionally not the game engine in the broad sense. It is the minimal execution base that owns:

- shared foundation primitives
- memory tracking baseline
- diagnostics and profiling baseline
- application lifecycle and microkernel services
- frame loop orchestration
- Windows platform runtime and native windowing
- per-frame input snapshot
- minimal presentation-oriented RHI
- D3D11 clear-screen backend
- support helpers for composition
- smoke apps and tests

## Module Breakdown

### Foundation

Lowest-level primitives and utility contracts:

- `Error`
- `Result<T>`
- ids and handles
- time/path helpers

No higher engine dependencies are allowed here.

### Memory

Memory baseline and tracking:

- allocator contracts
- tracking allocator
- allocation tags
- memory statistics and budgets
- `IMemoryTracker`

This module provides the observable memory baseline for diagnostics and future budget-aware systems.

### Diagnostics

Diagnostics services used across the base:

- logger interface
- log sinks/output
- profiling scopes
- counters used by frame loop, event bus, memory, and RHI baseline

Diagnostics must remain safe to disable without breaking the runtime.

### Core

Microkernel and lifecycle foundation:

- `Application`
- service container
- module registry
- event bus
- task scheduler
- main-thread dispatcher
- configuration service
- frame phases and frame context

`Core` orchestrates the runtime but does not know about Win32, input implementation details, D3D11, renderer systems, or gameplay.

### Platform

Windows-only platform layer:

- platform runtime
- window system
- native window
- message pump
- dynamic library loading
- high-resolution timer/process helpers

This layer exposes platform events upward without depending on gameplay or renderer logic.

### Input

Platform-neutral input contracts and implementation:

- input events
- key and mouse enums
- input snapshot
- keyboard and mouse state
- per-frame update from platform events

The layer intentionally stops at raw input meaning. Gameplay actions and bindings belong later in upper layers.

### RHI

Minimal rendering hardware interface boundary for this stage:

- device
- command context
- swap chain
- clear/present flow
- basic validation for presentation descriptors

This is not yet a renderer-grade abstraction. It deliberately excludes buffers, textures, materials, pipelines, and scene concepts from the stable baseline.

### RHI_D3D11

Windows D3D11 backend behind the RHI contracts:

- device creation
- swap chain creation
- render target setup
- clear/present
- resize handling

It is used only through the RHI boundary and support-layer helpers.

### Support

Official composition helper target for `EngineBase`.

This layer centralizes shared wiring such as:

- `RegisterEngineBase`
- `RegisterWindowsRuntime`
- `RegisterInputRuntime`
- `RegisterGraphicsRuntime`
- `CreateMainWindow`
- frame-loop helper registration
- default frame throttle and shared per-frame event containers

The goal is to keep smoke apps and future executable composition roots thin and scenario-focused.

## Dependency Rules

Dependency direction is one-way.

Expected shape:

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

Important rules:

- lower modules do not depend on higher modules
- `Core` does not depend on `Platform`, `Input`, `RHI`, or `RHI_D3D11`
- `Support` may depend on lower modules because it is composition glue
- smoke apps may contain scenario logic only, not reusable engine wiring

## Composition Model

Each executable under `EngineBase/Apps/*` is a composition root. It chooses which stable runtime pieces to register and which smoke scenario to run.

The reusable wiring is intentionally pushed into `EpidemicEngineBaseSupport`, so apps stay narrow:

- `HeadlessCoreApp`: lifecycle and frame-limit scenario
- `WindowSmokeApp`: window creation, platform event pump, close handling
- `InputSmokeApp`: platform + input update, input event logging, Escape-to-exit
- `RhiClearScreenApp`: D3D11 runtime, swap chain, clear screen, resize/minimize/restore handling

If a smoke app starts re-implementing generalized service registration or frame-loop setup that already exists in `Support`, that is considered architectural drift.

After successful `Initialize()`, the application seals its `ServiceContainer`, so future runtime layers cannot accidentally register new long-lived services in the middle of gameplay.

## Build and Validation

Build:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Run tests:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Run smoke apps:

```powershell
.\build\EngineBase\Apps\HeadlessCoreApp\EpidemicHeadlessCoreApp.exe
.\build\EngineBase\Apps\WindowSmokeApp\EpidemicWindowSmokeApp.exe
.\build\EngineBase\Apps\InputSmokeApp\EpidemicInputSmokeApp.exe
.\build\EngineBase\Apps\RhiClearScreenApp\EpidemicRhiClearScreenApp.exe
```

Logs:

```text
logs/epidemic.log
```

## What Is Explicitly Out of Scope

The following do not belong in `EngineBase`:

- asset pipelines and resource system
- virtual file system
- renderer architecture above clear/present baseline
- scene graph
- world/chunk runtime
- streaming and persistence systems
- gameplay framework
- quests, dialogue, inventory, NPC logic
- editor and tooling above smoke validation
- final game composition

## What Comes Next

After `EngineBase`, the next architectural layer should begin in a separate module family rather than expanding this one:

- `EngineRuntime`

Above that:

- `GameFramework`
- `Game`