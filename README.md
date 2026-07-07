# Epidemic Engine v1.0

`EngineBase` is the current stable runtime base of the repository. It contains the low-level engine contracts and the minimal executable baseline needed to bootstrap the engine, open a native window, process input, run a frame loop, and present through a minimal RHI boundary.

Today the repository is intentionally centered on `EngineBase/`. This layer includes:

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

`EngineBase` does not contain renderer-grade systems, asset/resource management, world runtime, gameplay framework, editor logic, or game-specific code. Those belong to the future upper layers:

- `EngineRuntime`
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