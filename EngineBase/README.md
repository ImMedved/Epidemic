# EngineBase

`EngineBase` is the stable runtime base of Epidemic Engine. It owns the low-level contracts, lifecycle, composition support, Windows platform layer, input snapshot, minimal RHI boundary, D3D11 clear-screen backend, smoke apps, and tests.

This layer is intentionally small. It is not the renderer, resource system, world runtime, gameplay framework, editor, or game code. Those systems are built above it.

## Modules

Current modules:

```text
Foundation
Memory
Diagnostics
Core
Platform
Input
RHI
RHI_D3D11
Support
Apps
Tests
```

`Support` is the official composition helper layer for apps and tests. It is a compiled target, not a legacy header-only include shim. Lower modules must not depend on it.

## Boundaries

`EngineBase` must not grow into upper engine systems. The following belong to later layers:

```text
resources, assets, VFS
renderer, frame graph, scene graph
world, streaming, chunks, save/persistence
physics gameplay model
audio, animation, weather, seasons, surface simulation
NPC, dialogue, quests, inventory, economy, skills
scripting, editor, game-specific code
```

Planned upper layers:

```text
EngineRuntime   - engine majors: resources, world/runtime simulation, renderer foundations, persistence
GameFramework   - reusable gameplay framework systems
Game            - final game composition root and game-specific logic
```

Detailed stability rules, dependency limits, ownership rules, and layer boundaries are documented in `docs/` and `EngineBase/API_STABILITY.md`.

## Build

From the repository root, using PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

For an already configured build:

```powershell
cmake --build build --config Debug
```

## Tests

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## Smoke Apps

After a Debug build, smoke apps are under `build/EngineBase/Apps/`.

```powershell
.\build\EngineBase\Apps\HeadlessCoreApp\EpidemicHeadlessCoreApp.exe
.\build\EngineBase\Apps\WindowSmokeApp\EpidemicWindowSmokeApp.exe
.\build\EngineBase\Apps\InputSmokeApp\EpidemicInputSmokeApp.exe
.\build\EngineBase\Apps\RhiClearScreenApp\EpidemicRhiClearScreenApp.exe
```

Logs are written to `logs/epidemic.log`.

## Development Rules

When changing `EngineBase`, keep the public API small and stable. Dependencies must flow downward only. Composition code belongs in `Support`, `Apps`, `Tests`, or upper executables, not in lower modules.

Use `Result<T>` for expected runtime failures. The support/composition API returns `Result` for window creation, graphics runtime creation, and swap-chain creation. Treat duplicate services, invalid lifecycle transitions, circular module dependencies, empty handlers, and similar contract violations as programming errors.`r`n`r`n`ServiceContainer` is sealed after successful `Initialize()`. Runtime services must be registered during composition, bootstrap, or initialize, not after startup.

`EngineBase` is currently Windows-only. The validated baseline is Win32 plus D3D11 on Windows 11. Cross-platform support is not part of this stage.