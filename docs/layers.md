# Engine Layers

This project is split into layers. Lower layers provide contracts and runtime infrastructure. Higher layers build engine systems and game logic on top.

## Layer Order

```text
EngineBase
  ↓
EngineRuntime
  ↓
GameFramework
  ↓
Game
```

Dependencies may only point downward. `EngineBase` must never include or link against `EngineRuntime`, `GameFramework`, or `Game`.

## EngineBase

`EngineBase` is the stable runtime foundation. It owns the low-level contracts required to start the engine, register services, run the lifecycle, process frames, open a Windows window, publish input snapshots, and present through a minimal RHI boundary.

It includes Foundation, Memory, Diagnostics, Core, Platform, Input, RHI, RHI_D3D11, Support, smoke apps, and tests.

`EngineBase` is intentionally not a full engine layer. It must not grow into resources, renderer, world streaming, save/load, physics gameplay, NPC logic, quests, scripting, editor tools, or game-specific code.

## EngineRuntime

`EngineRuntime` is the next layer. It will contain the engine majors: large reusable runtime systems that are still not gameplay-specific.

Expected future majors include resources, assets, serialization, persistence, world/streaming, scene/spatial runtime, renderer foundation, physics runtime, surface state, environment/time/weather foundation, navigation, animation, audio, scripting integration, and simulation runtime.

`EngineRuntime` may use `EngineBase` services such as the task scheduler, diagnostics, memory tracking, main-thread dispatcher, frame phases, and RHI. It must not know about the concrete game.

## GameFramework

`GameFramework` is the reusable gameplay framework layer.

It may define general gameplay concepts such as actors, items, equipment, inventory, interactions, dialogue framework, quest framework, factions, crime, vendors, schedules, needs, skills, and gameplay AI hooks.

It should use `EngineRuntime` systems instead of talking directly to platform, windowing, D3D11, or low-level orchestration.

## Game

`Game` is the final composition root. It chooses which engine systems and gameplay systems are used, registers game-specific modules, and contains concrete rules, content, balance, quests, NPC types, items, regions, and story logic.

Game-specific code may depend on all lower layers, but lower layers must not depend on it.

## Rule of Thumb

If a feature is required to start and orchestrate the runtime, it may belong in `EngineBase`.

If it is a reusable engine system such as resources, world, renderer, persistence, or simulation, it belongs in `EngineRuntime`.

If it describes general gameplay concepts, it belongs in `GameFramework`.

If it is specific to the actual game, it belongs in `Game`.
