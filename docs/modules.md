# Modules and Project Composition

## Active Step 1 structure

The repository is organized around `EngineBase/`.

### Implemented modules

- `Foundation`: `Error`, `Result`, `Path`, `StringId`, `NameId`, `Handle<T>`
- `Diagnostics`: logging contracts and `ConsoleLogger`
- `Core`: application lifecycle, service container, module registry, event bus, task scheduler, configuration
- `Platform`: `IPlatformRuntime`, `IDynamicLibrary`, Windows runtime implementation

### Prepared module targets

These module targets already exist and are intentionally empty placeholders until later plan steps:

- `Memory`
- `Input`
- `RHI`
- `RHI_D3D11`

### Smoke apps

- `EngineBase/Apps/HeadlessCoreApp`
- `EngineBase/Apps/WindowSmokeApp`
- `EngineBase/Apps/InputSmokeApp`
- `EngineBase/Apps/RhiClearScreenApp`

### Tests

- `EngineBase/Tests/Unit`
- `EngineBase/Tests/Integration`
- `EngineBase/Tests/Regression`