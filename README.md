# Epidemic Engine v1.0

`EngineBase` is the stable low-level engine base of the repository. It contains the engine contracts and the minimal executable baseline needed to bootstrap the engine, open a native window, process input, run a frame loop, and present through a minimal RHI boundary.

`EngineRuntime` is the modular runtime layer above EngineBase. It owns runtime systems, shared runtime state, backend-facing contracts and Support composition, but it does not contain gameplay rules or concrete game content.

`EngineBase/` includes:

- `Foundation`
- `Memory`
- `Diagnostics`
- `Core`
- `Platform`
- `Input`
- `RHI`
- `RHI_D3D11`
- `Support`
- smoke apps and grouped tests

`EngineRuntime/` includes:

- `RuntimeFoundation`
- `Assets`
- `Resources`
- `Serialization`
- `Persistence`
- `Time`
- `Environment`
- `Scene`
- `World`
- `Streaming`
- `Renderer`
- `Physics`
- `Navigation`
- `Animation`
- `Audio`
- `Simulation`
- `Support`

Gameplay and final product behavior belong above these layers:

- `GameFramework`
- `Game`

## Build

From the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

## Tests

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Runtime-focused tests can be filtered with:

```powershell
ctest --test-dir build -R EpidemicRuntime --output-on-failure
```

## Smoke Apps

After a Debug build, the required smoke apps are available under `build/EngineBase/Apps/`:

```powershell
.\build\EngineBase\Apps\HeadlessCoreApp\EpidemicHeadlessCoreApp.exe
.\build\EngineBase\Apps\WindowSmokeApp\EpidemicWindowSmokeApp.exe
.\build\EngineBase\Apps\InputSmokeApp\EpidemicInputSmokeApp.exe
.\build\EngineBase\Apps\RhiClearScreenApp\EpidemicRhiClearScreenApp.exe
```

Logs are written to `logs/epidemic.log`.

## Dependency Direction

When adding a new low-level module, dependencies must still flow downward toward `Foundation`. Composition and runtime wiring belong in `EngineBase/Support`, smoke apps, tests, or future upper executable layers, not in lower engine modules.

## Windows-only Status

The current validated baseline is Windows-only. `Platform` is implemented through Win32, and the shipping graphics backend in this stage is `RHI_D3D11`.

## More Documentation

Detailed `EngineBase` module documentation lives in:

- `EngineBase/README.md`
- `EngineBase/API_STABILITY.md`
- `docs/base/README.md`

Detailed `EngineRuntime` documentation lives in:

- `docs/runtime/README.md`
- `docs/runtime/api_stability.md`
- `docs/runtime/using_runtime.md`
