# Microkernel Status

The current microkernel implementation lives in `EngineBase/Core` and is already separated from higher-level subsystems.

## Included in the current slice

- application lifecycle: `bootstrap -> initialize -> run -> shutdown`
- typed service container
- dependency-aware module registry
- queued and synchronous event bus
- baseline task scheduler
- diagnostics-backed startup and shutdown flow

## Outside the current slice

- window system
- input processing
- RHI abstraction
- D3D11 backend
- resource management and gameplay systems

Those systems now have structural placeholders under `EngineBase/`, but their real implementation belongs to later plan steps.