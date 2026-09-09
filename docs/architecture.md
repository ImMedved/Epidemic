# Архитектура Epidemic Engine

Проект разделен на четыре слоя с односторонним направлением зависимостей:

```text
EngineBase
    ↓
EngineRuntime
    ↓
GameFramework
    ↓
Game
```

Нижний слой не знает о существовании верхнего. Это правило важнее удобства конкретной реализации: если новая возможность относится к gameplay или к отдельному engine subsystem, она не должна протягиваться вниз только потому, что там уже есть подходящий сервис.

## EngineBase

EngineBase — стабильная платформа исполнения. Он отвечает за то, без чего невозможно запустить остальные части движка: базовые типы и ошибки, memory/diagnostics baseline, `Application` и service/module lifecycle, scheduler и main-thread dispatcher, Win32 runtime и окна, raw input, минимальный presentation RHI и D3D11 backend.

EngineBase не содержит world, resources, renderer, physics, animation, audio, simulation или gameplay. `EngineBase/Support` только собирает готовые нижние сервисы для executable и не становится самостоятельной системой.

## EngineRuntime

EngineRuntime содержит переиспользуемые engine systems, которые уже выше platform foundation, но еще не выражают правила конкретной игры. Каждый major является отдельной библиотекой и может собираться и тестироваться независимо от соседних majors.

```text
RuntimeFoundation
Assets
Resources
Serialization
Persistence
Time
Environment
Scene
World
Streaming
Renderer
Physics
Navigation
Animation
Audio
Simulation
```

Majors не вызывают друг друга напрямую. Например, Physics не знает Scene, Animation не знает Renderer, Audio не знает Resources, а Streaming не знает World/Persistence/Resources. Они объявляют собственные ports. `EngineRuntime/Support` создает typed adapters, соединяющие эти ports в полноценный runtime.

Такое разделение означает, что Physics, Renderer, Audio, Navigation или Simulation можно заменить, тестировать и развивать независимо, пока сохраняются их public contracts.

## Runtime Support

Runtime Support — composition layer, а не еще один major. Он атомарно создает `EngineRuntimeServices`, владеет adapters и `IEngineRuntimeCoordinator`, проводит межмодульные проекции и управляет общим shutdown.

Стандартные технические связи принадлежат Support, например:

```text
Scene → Renderer
Resources → Renderer
Scene ↔ Physics
Resources → Animation → Renderer
Resources → Audio
World + Resources + Persistence → Streaming
Environment → Navigation
Time → Simulation → commit target
```

Support не хранит вторую копию authoritative World/Scene/Resources state и не добавляет gameplay rules.

## GameFramework

GameFramework — следующий слой. Здесь должны появиться общие игровые понятия: actors, interaction, items/inventory, factions, crime, dialogue/quests, NPC behavior, economy, character progression, ships и другие reusable gameplay systems.

GameFramework использует Runtime contracts и связывает `RuntimeObjectId` с собственными domain records. Он не должен обращаться напрямую к Win32, D3D11 или внутренним реализациям Runtime majors.

## Game

Game — конечный composition root и конкретная игра. Здесь находятся баланс, контент, сюжет, конкретные фракции, NPC, предметы, регионы и правила мира. Он может использовать все нижние слои, но ни один нижний слой не зависит от него.

## Как выбирать слой для нового кода

Если код нужен для запуска и общей инфраструктуры процесса — это кандидат в EngineBase. Если это нейтральный переиспользуемый engine subsystem — EngineRuntime. Если код описывает общую игровую механику — GameFramework. Если он выражает конкретное правило или контент текущей игры — Game.

Новые зависимости между Runtime majors не добавляются. Если два majors необходимо связать технически, сначала проверяется возможность adapter в Support. Если связь имеет gameplay-смысл, она поднимается в GameFramework.

## Architecture freeze

Layer boundaries are enforced by `cmake/ArchitectureFreeze.cmake` during every CMake configuration. The validator inspects both direct and interface link dependencies, scans production includes, and compiles every public header as an isolated translation unit.

The frozen dependency rules are:

- EngineBase has no Runtime or GameFramework dependencies.
- EngineRuntime has no GameFramework dependencies. Runtime majors may depend on RuntimeFoundation; cross-major composition belongs to Runtime Support.
- GameFramework base infrastructure and gameplay state owners have no direct Runtime dependencies.
- RuntimeBridge is the approved world, physics, environment, and navigation boundary.
- CoreIntegration's RuntimeTimeAdapter is the sole approved direct IntegrationLayer dependency on RuntimeTime. It adapts the runtime clock to gameplay time and must not acquire additional Runtime responsibilities.
- Gameplay majors may depend only on the centrally allowlisted Framework foundation modules; horizontal gameplay composition belongs to IntegrationLayer.

Changing an allowlist is an architecture decision and must update the validator, this document, and the architecture build matrix in the same change.