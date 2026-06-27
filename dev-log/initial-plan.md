# Начальный план

## Видение

`Epidemic Engine v1.0` создается как новый runtime для полностью новой игры. Внутренняя архитектура строится с нуля как модульная, тестируемая и пригодная для долгого развития под современный renderer, большие миры, сложную симуляцию и тяжелые asset pipeline.

## Базовые архитектурные принципы

- минимальное и строгое микроядро без игровой логики внутри;
- явные границы между `foundation`, `core`, `layers`, `apps` и `tests`;
- typed interfaces вместо строковых сервисных контрактов;
- минимум глобального mutable state;
- ownership через RAII и стандартные контейнеры;
- асинхронность, streaming и масштабирование закладываются заранее;
- каждая новая подсистема закрепляется тестами и понятной структурой каталогов.

## Целевая форма runtime

### 1. Microkernel

- [x] Жизненный цикл приложения `bootstrap -> initialize -> run -> shutdown`
- [x] Реестр модулей и вычисление dependency-aware порядка запуска
- [x] Typed service graph
- [x] Sync/queued event bus
- [x] Task scheduler bootstrap
- [x] Diagnostics bootstrap
- [x] Configuration bootstrap
- [x] Обратный порядок shutdown
- [x] Изоляция от renderer, gameplay и content-систем

Статус: базовый блок завершен и может долго не меняться.

### 2. Foundation Layer

- [ ] memory allocators и ownership helpers
- [ ] string/encoding utilities
- [x] containers/handles/id helpers
- [ ] math library
- [x] filesystem helpers
- [x] error/result types
- [ ] serialization helpers
- [ ] shared reflection hooks

Статус: реализован минимальный рабочий slice.

Ближайший минимальный набор:
- [x] error/result types
- [x] filesystem path helpers
- [x] string/id helpers
- [x] handle/generation id helpers
- [ ] базовые diagnostics primitives

Остальное остается отложенным до появления реальных потребителей:
- [ ] memory allocators
- [ ] custom containers
- [ ] math library
- [ ] serialization helpers
- [ ] reflection hooks

### 3. Platform Layer

- [x] platform-agnostic runtime interface
- [x] Windows-first runtime implementation
- [x] process startup
- [ ] Win32 windowing
- [ ] input polling/mapping
- [ ] file watching
- [x] timers
- [ ] clipboard
- [ ] monitor and DPI
- [ ] crash handling
- [x] dynamic library loading

Статус: реализован headless Windows-first slice без окна и input.

### 4. RHI and Renderer Layer

- [ ] абстракция `RHI`
- [ ] первый backend `RHI.D3D11`
- [ ] `Renderer` поверх RHI
- [ ] frame graph
- [ ] scene submission
- [ ] materials, lighting, UI, post-process, debug draw
- [ ] готовая граница для Vulkan

Статус: не начато.

### 5. Runtime Services Layer

- [ ] virtual file system
- [ ] resource manager
- [ ] asset metadata database
- [ ] world streaming manager
- [ ] scene system
- [ ] animation system
- [ ] audio system
- [ ] physics/collision
- [ ] UI framework
- [ ] font subsystem
- [ ] save/load system
- [ ] networking hooks
- [x] scripting host interface
- [ ] scripting host implementation
- [ ] diagnostics/telemetry extensions

Статус: пока есть только контракты и placeholder-реализации.

### 6. Gameplay Framework Layer

- [ ] gameplay world bootstrap
- [ ] entity/system contracts
- [ ] scene ownership model
- [ ] input-to-gameplay bridge
- [ ] game state flow
- [ ] content-driven feature modules

Статус: не начато.

### 7. Tools and Content Pipeline

- [ ] asset import pipeline
- [ ] resource cooking
- [ ] editor/tooling bootstrap
- [ ] content validation
- [ ] hot-reload friendly build steps

Статус: не начато.

## Карта модулей проекта

- [x] `src/foundation/*` — базовые value-oriented primitives
- [x] `src/core/*` — микроядро и его внутренние подсистемы
- [x] `src/layers/platform/*` — platform contracts и Windows-first runtime slice
- [x] `src/layers/runtime/*` — runtime contracts и placeholder implementations
- [x] `src/apps/*` — исполняемые хосты
- [x] `tests/*` — unit, integration и regression tests
- [ ] `src/layers/rhi/*`
- [ ] `src/layers/renderer/*`
- [ ] `src/layers/gameplay/*`
- [ ] `src/layers/tools/*`

## Стратегия typed service container

- [x] interface-based registration
- [x] typed lookup
- [x] scoped ownership через `std::shared_ptr`
- [x] запрет null registration
- [x] защита от duplicate registration
- [x] тесты на register/get/contains/error paths
- [ ] lazy initialization
- [ ] startup descriptors
- [ ] startup graph diagnostics export

## Стратегия event system

- [x] typed native events
- [x] sync events
- [x] queued events
- [x] queued event drain
- [x] FIFO-базовый контракт для queued dispatch
- [x] тесты на sync/queued dispatch
- [ ] deterministic ordering modes beyond baseline
- [ ] latency/origin tracing

## Стратегия VFS

- [ ] mount table
- [ ] normalized virtual paths
- [x] общий path-тип `Path`
- [ ] fast lookup cache
- [ ] async reads
- [ ] streaming handles
- [ ] file watching
- [ ] content hashing
- [ ] package/archive support

## Стратегия Resource Manager

- [ ] resource ids
- [ ] typed loaders
- [ ] dependency graph
- [ ] streaming states
- [ ] background decode
- [ ] GPU upload queue
- [ ] residency budgets
- [ ] hot reload
- [x] foundation для handle-based ownership

## Открытый мир и сложная симуляция

- [ ] world partitioning и streaming cells
- [ ] entity activation by relevance
- [ ] async background loading
- [ ] simulation LOD
- [ ] saveable world state deltas
- [ ] deterministic critical state handling

## Готовность к 4K и современным asset pipelines

- [ ] texture residency budgets
- [ ] mip streaming
- [ ] optional virtual texturing
- [ ] upload throttling
- [ ] format conversion pipeline
- [ ] compressed textures
- [ ] asset metadata for memory and priority

## Хостинг скриптов

- [x] интерфейс `IScriptHost`
- [x] stub-реализация без VM
- [ ] VM host
- [ ] binding layer
- [ ] script diagnostics и performance hooks

## Итеративный план поставки

### Phase 0. Bootstrap репозитория

- [x] инициализировать новый repo в `Epidemic engine v1.0`
- [x] создать docs и базовую структуру
- [x] зафиксировать branch model `main` / `dev`

### Phase 1. Kernel Bootstrap

- [x] entry point executable
- [x] базовый core-каркас микроядра
- [x] logging
- [x] configuration service
- [x] module system
- [x] typed service container
- [x] startup/shutdown graph
- [x] sync/queued events
- [x] task scheduler stub
- [x] placeholder interfaces для runtime-сервисов
- [x] unit и integration tests для container, lifecycle, events и scheduler

Результат:
- [x] headless bootstrap стартует, выполняет модули в правильном порядке и корректно завершается

### Phase 2. Foundation Slice

- [x] `Result` / `Error`
- [x] `Path`
- [x] `StringId` / `NameId`
- [x] `Handle<T>`
- [x] базовые тесты и регрессионные проверки

Результат:
- [x] foundation уже используется в runtime-контрактах и test suite

### Phase 3. Platform Slice

- [x] platform runtime interface
- [x] Windows-first implementation
- [x] process info
- [x] monotonic clock
- [x] dynamic library loading
- [x] symbol lookup
- [x] platform tests и regression tests

Результат:
- [x] platform слой уже является реальной частью composition root

### Phase 4. DX11 Bootstrap

- [ ] DX11 device bootstrap
- [ ] swap chain
- [ ] command submission abstraction
- [ ] debug layer integration

Результат:
- [ ] clear-screen sample через RHI abstraction

### Phase 5. VFS + Resource Core

- [ ] mount system
- [ ] virtual paths
- [ ] async file IO
- [ ] resource handles
- [ ] loader registry
- [ ] texture loading foundation

Результат:
- [ ] runtime умеет находить и читать данные через virtual paths

### Phase 6. Gameplay Framework

- [ ] gameplay world bootstrap
- [ ] scene ownership
- [ ] input bridge
- [ ] first gameplay state flow

Результат:
- [ ] минимальный playable slice новой игры

## Правила зависимостей

- [x] `foundation` находится ниже `core` и `layers`
- [x] `core` не зависит от gameplay и renderer
- [x] `layers/*` подключаются к `core` через явные interfaces и contracts
- [x] `apps/*` только собирают runtime composition root
- [x] `tests/*` могут зависеть от `foundation`, `core` и конкретных слоев
- [ ] renderer зависит от RHI, но не наоборот

## Стратегия тестирования

- [x] tests для foundation slice
- [x] tests для platform slice
- [x] unit tests для service container
- [x] integration tests для module lifecycle
- [x] tests для sync/queued events
- [x] tests для task scheduler
- [x] regression tests для null registration, duplicate registration, duplicate module id, missing dependency, circular dependency и platform error paths
- [x] application smoke tests
- [ ] render smoke tests
- [ ] golden-data tests
- [ ] streaming/simulation stress tests

## Ближайший следующий вектор

0. [x] минимальный Foundation slice: `Result/Error`, `Path`, `StringId/NameId`, `Handle<T>`
1. [x] Platform layer skeleton
2. [ ] DX11 bootstrap
3. [ ] Runtime VFS skeleton
4. [ ] Resource Manager contracts
5. [ ] Gameplay framework skeleton

Текущий итог:

- [x] Минимальный backbone нового runtime уже существует и собирается
- [x] Foundation, Core и Platform выделены как отдельные архитектурные зоны
- [x] Проект покрыт базовыми и регрессионными тестами и может безопасно двигаться дальше
