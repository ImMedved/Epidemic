# EngineBase API Stability

## Architectural Philosophy

EngineBase is designed as a stable runtime platform rather than as a feature-rich engine layer.

Whenever a new feature is proposed, the first question should be:

"Does this belong to the runtime foundation, or should it live in a higher layer?"

When in doubt, prefer keeping EngineBase smaller.

## 1. Purpose of EngineBase

`EngineBase` is the lowest stable runtime/framework layer of the engine. It provides foundation primitives, memory baseline, diagnostics, lifecycle orchestration, service registration, module orchestration, frame phases, Windows platform runtime, input snapshotting, minimal presentation RHI, a D3D11 clear-screen backend, smoke apps, and tests.

`EngineBase` exists to give upper layers a stable runtime base. It is not the place where future game systems are implemented.

## 2. Future Layer Boundaries

Planned dependency direction:

- `EngineBase` does not depend on `EngineRuntime`, `GameFramework`, or `Game`.
- `EngineRuntime` may depend on `EngineBase`.
- `GameFramework` may depend on `EngineRuntime` and the stable primitives of `EngineBase`.
- `Game` is the final composition root and may depend on all lower layers.

EngineBase must not implement higher-level engine systems. This includes, but is not limited to:

- resource and asset management
- world representation and streaming
- rendering systems
- persistence and save systems
- gameplay physics
- environment simulation
- gameplay framework
- scripting
- editor functionality
- game-specific code

## 3. Stable Contracts

Stable public contracts at this stage:

- `Foundation` primitives, ids, handles, paths, time wrappers, `Result<T>`, and `Error`
- `Memory` baseline allocators and tracking interfaces
- `Diagnostics` logger, counters, profiling baseline, thread naming helpers
- `Core` application lifecycle, service container, module registry, event bus, task scheduler, main-thread dispatcher, frame phases
- `Platform` Windows runtime, dynamic library loading, window system, platform events
- `Input` snapshot and event translation contracts
- `RHI` minimal presentation boundary: device, command context, swap chain, descriptors, clear/present/resize flow
- EngineBase support/composition helpers for smoke apps, tests, and future upper executables

## 4. Semi-stable Contracts

Semi-stable areas may still receive small clarifications without changing the architectural direction:

- diagnostics counter set and naming
- exact smoke app wiring helpers
- diagnostics message wording
- internal helper services such as per-frame platform event storage
- composition helper APIs

## 5. Internal Implementation Details

The following details are internal and must not be treated as stable extension points:

- concrete scheduler worker implementation
- concrete dispatcher queue implementation
- concrete Win32 window class details
- console logger file path mechanics
- D3D11 backend internal state objects
- helper structures used only to bridge frame-loop composition

## 6. Forbidden Dependencies

Dependency rules inside `EngineBase`:

- lower modules must not depend on future upper layers
- `Foundation` must not depend on other EngineBase modules and should remain self-contained
- `Memory` must not depend on `Diagnostics`, `Core`, `Platform`, `Input`, `RHI`, `Support`, or `Apps`
- `Core` may depend on lower layers only
- `Platform`, `Input`, and `RHI` may depend only on lower runtime layers they actually need
- `EpidemicEngineBaseSupport` is a composition/helper target, not a lower runtime layer
- no lower module may depend on `EpidemicEngineBaseSupport`
- `Apps`, `Tests`, and future upper executables may depend on `EpidemicEngineBaseSupport`

## 7. Service Ownership Rules

Long-lived runtime services after `RegisterEngineBase`:

- `diagnostics::ILogger`
- `core::config::IConfiguration`
- `core::events::IEventBus`
- `core::tasks::ITaskScheduler`
- `core::IMainThreadDispatcher`
- `memory::IMemoryTracker`

Long-lived runtime services after `RegisterWindowsRuntime`:

- `platform::IPlatformRuntime`
- `platform::IWindowSystem`

Long-lived runtime services after `RegisterInputRuntime`:

- `input::IInputSystem`

Long-lived runtime services after `RegisterGraphicsRuntime`:

- `rhi::IRhiDevice`
- `rhi::IRhiCommandContext`

Long-lived runtime services after `RegisterMainSwapChain`:

- `rhi::IRhiSwapChain`

Ownership rules:

- `Application` owns the `ServiceContainer` and `ModuleRegistry`
- `ServiceContainer` owns long-lived runtime services through `std::shared_ptr`
- modules may register services only during composition/bootstrap/initialize stages
- runtime services are expected to be registered during bootstrap
- runtime registration after startup is considered exceptional and must be explicitly documented
- window system owns platform window implementation details
- a concrete `IWindow` lifetime must outlive the swap chain created for it
- the RHI device owns backend device state
- a swap chain must not outlive its native window
- D3D11 types must not leak into the stable RHI public API
- raw owning pointers are forbidden

## 8. Pointer/Reference Ownership Rules

- `std::unique_ptr`: exclusive ownership for modules and private implementation objects
- `std::shared_ptr`: shared long-lived runtime services stored in `ServiceContainer`
- `Handle<T>`: stable reference to engine-managed object/resource without exposing ownership
- raw owning pointers: forbidden
- `std::weak_ptr`: non-owning back-reference when a `shared_ptr` cycle is possible
- `T&`: required dependency that must outlive the current object
- `T*`: nullable non-owning dependency only when `null` is meaningful

## 9. Result vs Exceptions Policy

- `Result<T>` is used for expected runtime failures such as platform operations, window creation, backend creation, swap chain creation, present/resize failures, or invalid external descriptors
- exceptions or assertions are used for engine contract violations such as missing required services, invalid lifecycle transitions, or empty callbacks/tasks where the caller violated the engine contract
- exceptions are not used for expected runtime failures

## 10. Threading and Main-Thread Policy

Main thread responsibilities:

- application lifecycle orchestration
- `Bootstrap`, `Initialize`, `Run`, `Shutdown`
- window message pump
- platform event collection
- input snapshot publication
- frame phase orchestration
- `EventBus::DrainQueued`
- `IMainThreadDispatcher::Drain`
- module tick entry points
- `RHI BeginFrame / EndFrame / Present`
- swap chain resize
- smoke app scenario code

Worker-thread rules:

- worker execution goes through `core::tasks::ITaskScheduler`
- worker threads may execute generic CPU jobs and background preparation work
- worker threads must not create or destroy windows
- worker threads must not pump Win32 messages
- worker threads must not call `Present`
- worker threads must not resize swap chains
- worker threads must not directly use the D3D11 immediate context
- subsystems must not create long-lived `std::thread` instances without a separate architecture review

## 11. Frame Phase Policy

Official baseline phase order:

1. `BeginFrame`
2. `PumpPlatformEvents`
3. `UpdateInput`
4. `DrainEvents`
5. `RunScheduledMainThreadTasks`
6. `TickModules`
7. `RhiBeginFrame`
8. `RhiEndFrame`
9. `Present`
10. `EndFrame`

Rules:

- main-thread tasks execute only in `RunScheduledMainThreadTasks`
- platform pumping is main-thread-only
- input publication happens inside the frame loop
- RHI frame boundaries stay in the dedicated RHI phases
- smoke apps may attach work around these phases but must not invent a second runtime loop

## 12. RHI/Backend Policy

EngineBase exposes only the minimal GPU boundary required to initialize the graphics subsystem and present frames.

It is intentionally not a rendering framework. Stable baseline intent:

- backend-agnostic `IRhiDevice`, `IRhiCommandContext`, `IRhiSwapChain`
- descriptor validation
- `BeginFrame`, `Clear`, `EndFrame`, `Present`, `Resize`
- backend name reporting
- `Result/Error` based failure reporting for expected runtime issues

Policy:

- D3D11 creation is routed through the support/composition layer for smoke apps and future executables
- backend-specific types stay inside backend modules
- renderer-grade GPU resources, materials, scene rendering, and higher rendering systems belong to future upper layers or future reviewed extensions

## 13. Testing and Freeze Rules

- expanding `EngineBase` into future gameplay/runtime systems is forbidden without architecture review
- fixes, documentation clarifications, targeted tests, and narrowly scoped contract improvements are allowed
- any lower-layer API change should be treated as a stability-sensitive change and justified explicitly
- smoke apps and automated tests are part of the freeze surface and should stay runnable after each contract change
- any architectural change that affects dependency direction or layer boundaries requires an architecture review before implementation