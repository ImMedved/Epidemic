# Начальный план

## Видение

`Epidemic Engine v1.0` создается как новый runtime, который сохраняет совместимость со Storm на внешней границе, но полностью заменяет внутреннюю архитектуру. Старый движок остается рядом как спецификация поведения, форматов данных и ожидаемых контрактов, а не как кодовая основа.

## Базовые архитектурные принципы

- минимальное и строгое микроядро без привязки к игровым системам;
- совместимость решается адаптерами, а не переносом старых архитектурных ошибок;
- платформенные, графические, ресурсные, скриптовые и симуляционные подсистемы изолируются в модули и слои;
- максимум локального владения данными, минимум глобального mutable state;
- проектирование под асинхронную загрузку, streaming и большие миры с первого дня;
- детерминированность там, где это критично для логики, сейвов, событий и тестов.

## Целевая форма runtime

### 1. Microkernel

- [x] Владеет жизненным циклом приложения.
- [x] Владеет регистрацией модулей и вычислением порядка запуска.
- [x] Строит typed service graph.
- [x] Инициализирует sync/queued event bus.
- [x] Инициализирует task scheduler.
- [x] Инициализирует diagnostics.
- [x] Инициализирует configuration service.
- [x] Управляет завершением модулей в обратном порядке.
- [x] Не знает о Storm-логике, DX11, форматах данных и legacy service names.

Статус блока: выполнен в первом рабочем приближении. К микроядру можно долго не возвращаться, пока не появятся новые реальные требования со стороны платформы, compatibility layer или hot-reload модулей.

### 2. Foundation Layer
- [ ] memory allocators и ownership helpers
- [ ] string/encoding utilities
- [ ] containers/handles/id helpers
- [ ] math library
- [ ] filesystem helpers
- [ ] error/result types
- [ ] serialization helpers
- [ ] shared reflection hooks

Статус: инкрементальная реализация. Foundation не выделяется сейчас в большой самостоятельный этап, чтобы не проектировать абстракции без реальных потребителей. Базовые примитивы выносятся в Foundation по мере появления требований от platform, VFS, resource manager, renderer и compatibility layer.

Ближайший минимальный набор:
- [ ] error/result types
- [ ] filesystem path helpers
- [ ] string/id helpers
- [ ] handle/generation id helpers
- [ ] базовые diagnostics primitives

Остальное остается отложенным до появления реальных потребителей:
- [ ] memory allocators
- [ ] custom containers
- [ ] math library
- [ ] serialization helpers
- [ ] reflection hooks

### 3. Platform Layer

- [ ] process startup
- [ ] Win32 windowing
- [ ] input polling/mapping
- [ ] file watching
- [ ] timers
- [ ] clipboard
- [ ] monitor and DPI
- [ ] crash handling
- [ ] dynamic library loading

Статус: не начато. Windows-first остается целевым направлением.

### 4. RHI and Renderer Layer

- [ ] абстракция `RHI`
- [ ] первый backend `RHI.D3D11`
- [ ] `Renderer` поверх RHI
- [ ] frame graph
- [ ] scene submission
- [ ] materials/lighting/UI/post-process/debug draw
- [ ] future-ready boundary for Vulkan

Статус: не начато. В текущем шаге оставлен только placeholder interface.

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
- [ ] scripting host integration
- [ ] diagnostics/telemetry extensions

Статус: не начато. Пока есть только placeholder interfaces для `VFS`, `ResourceManager`, `Renderer`, `ScriptHost`.

### 6. Compatibility Layer

- [ ] Storm service-name adapter
- [ ] `ATTRIBUTES` facade
- [ ] `MESSAGE` facade
- [ ] legacy event adapter
- [ ] script host bridge
- [ ] path/VFS alias resolver
- [ ] legacy format loaders
- [ ] INI compatibility parser
- [ ] font/text compatibility adapter
- [ ] input command-name compatibility table

Статус: не начато по договоренности.

### 7. Gameplay Host Layer

- [ ] script VM runtime
- [ ] engine-call bindings
- [ ] quest/event dispatch bridge
- [ ] savegame object binding
- [ ] legacy UI binding
- [ ] character/sea/world/inventory/dialog/battle/location adapters

Статус: не начато.

## Карта модулей проекта

- [x] `src/core/*` — микроядро и его внутренние подсистемы
- [x] `src/layers/runtime/*` — runtime-layer contracts и placeholder implementations
- [x] `src/apps/*` — исполняемые хосты
- [x] `tests/*` — unit/integration tests
- [ ] `src/layers/platform/*`
- [ ] `src/layers/rhi/*`
- [ ] `src/layers/renderer/*`
- [ ] `src/layers/compatibility/*`
- [ ] `src/layers/gameplay_host/*`
- [ ] `tools/*`

## Стратегия typed service container

- [x] interface-based registration
- [x] typed lookup
- [x] scoped ownership через `std::shared_ptr`
- [x] запрет null-instance registration
- [x] защита от double registration
- [x] тесты на register/get/contains
- [ ] lazy initialization
- [ ] startup descriptors
- [ ] compatibility aliases по строковым именам
- [ ] startup graph diagnostics export

## Стратегия event system

- [x] typed native events
- [x] sync events
- [x] queued events
- [x] queued event drain
- [x] тесты на sync/queued dispatch
- [ ] legacy string event bridge
- [ ] deterministic ordering modes beyond current baseline
- [ ] latency/origin tracing

## Стратегия VFS

- [ ] mount table
- [ ] normalized virtual paths
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
- [ ] handle-based ownership

## Совместимость со старыми форматами и API

- [ ] old scripts
- [ ] old resources
- [ ] old INI semantics
- [ ] `ATTRIBUTES`
- [ ] `MESSAGE`
- [ ] old service names

## Открытый мир и сложная симуляция

- [ ] world partitioning/streaming cells
- [ ] entity activation by relevance
- [ ] async background loading
- [ ] simulation LOD
- [ ] saveable world state deltas
- [ ] deterministic quest-critical state handling

## Готовность к 4K и современным asset pipelines

- [ ] texture residency budgets
- [ ] mip streaming
- [ ] optional virtual texturing
- [ ] upload throttling
- [ ] format conversion pipeline
- [ ] compressed textures
- [ ] asset metadata for memory/priority

## Хостинг скриптов и legacy game logic

- [x] интерфейс `IScriptHost`
- [x] stub-реализация без VM
- [ ] VM host
- [ ] binding layer
- [ ] legacy API adapters
- [ ] script diagnostics/performance hooks

## Итеративный план поставки

### Phase 0. Bootstrap репозитория

- [x] инициализировать новый repo в `Epidemic engine v1.0`
- [x] создать docs и базовую структуру
- [x] оставить старый Storm Engine рядом как reference
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
- [x] placeholder interfaces для `VFS`, `ResourceManager`, `Renderer`, `ScriptHost`
- [x] unit/integration tests на container, lifecycle, events, scheduler

Результат:

- [x] headless bootstrap стартует, выполняет модули в правильном порядке и корректно завершается

### Phase 2. Platform + DX11 RHI

- [ ] Win32 platform layer
- [ ] DX11 device bootstrap
- [ ] swap chain
- [ ] command submission abstraction
- [ ] debug layer integration

Результат:

- [ ] clear-screen sample через RHI abstraction

### Phase 3. VFS + Resource Core

- [ ] mount system
- [ ] virtual paths
- [ ] async file IO
- [ ] resource handles
- [ ] loader registry
- [ ] texture/INI loading foundation

Результат:

- [ ] runtime умеет находить и читать legacy files через virtual paths

### Phase 4. Compatibility Core

- [ ] old service-name lookup adapter
- [ ] `ATTRIBUTES` facade
- [ ] `MESSAGE` facade
- [ ] legacy event bridge
- [ ] path alias compatibility

Результат:

- [ ] old-style engine-facing calls мапятся в новые контракты

### Phase 5. Script Host Bridge

- [ ] VM bootstrap
- [ ] legacy function bindings
- [ ] script event dispatch
- [ ] diagnostics/tracing

Результат:

- [ ] можно запускать изолированные legacy scripts в новом runtime shell

### Phase 6. First Visual Content

- [ ] texture loader
- [ ] font subsystem
- [ ] model reader
- [ ] basic scene submission
- [ ] simple camera/debug UI

Результат:

- [ ] legacy assets рендерятся в test scene/viewer

### Phase 7. Gameplay Vertical Slice

- [ ] location loading
- [ ] player input mapping
- [ ] one interaction flow
- [ ] one UI screen
- [ ] save/load slice

Результат:

- [ ] маленький playable compatibility slice

## Правила зависимостей

- [x] `core` не зависит от compatibility и gameplay host
- [x] `layers/*` подключаются к `core` через явные interfaces и contracts
- [x] `apps/*` только собирают runtime composition root
- [x] `tests/*` могут зависеть от `core` и конкретных слоев
- [ ] compatibility layer может зависеть от runtime services, но не от platform internals напрямую
- [ ] renderer зависит от RHI, но не наоборот

## Стратегия тестирования

- [x] unit tests для service container
- [x] integration tests для module lifecycle
- [x] tests для sync/queued events
- [x] tests для task scheduler
- [ ] contract tests against old behavior
- [ ] render smoke tests
- [ ] golden-data tests
- [ ] streaming/simulation stress tests

## Ближайший следующий вектор

0. [ ] минимальный Foundation slice: Result/Error, Path, StringId/NameId, Handle<T>
1. [ ] platform layer skeleton
2. [ ] DX11 bootstrap
3. [ ] runtime VFS skeleton
4. [ ] resource manager contracts
5. [ ] compatibility layer skeleton

Текущий итог:

- [x] Минимальный backbone нового runtime уже существует и собирается.
- [x] Микроядро выделено отдельно от слоев.
- [x] Мы можем двигаться дальше без возврата к базовой структуре.
