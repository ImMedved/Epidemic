# Толковый словарь терминологии Epidemic Engine

Документ сводит терминологию нижней базы движка и будущего runtime-слоя в единый сокращенный словарь. Структура намеренно словарная: сначала идут понятия `EngineBase`, затем понятия `EngineRuntime`. Внутри каждой крупной категории термины отсортированы по алфавиту.

---

# I. Толковый словарь терминологии базы

## 1. Архитектура, слои и границы

### API Stability
Правила стабильности публичных контрактов `EngineBase`. Определяет, какие интерфейсы считаются стабильными, какие детали остаются внутренними, какие зависимости запрещены и какие изменения требуют отдельного архитектурного пересмотра.

### Application
Главный объект нижнего runtime-каркаса. Управляет жизненным циклом, registry модулей, сервисами, frame loop и остановкой приложения. Не должен напрямую знать про renderer, gameplay, resources, world или scripting.

### Composition Root
Место, где конкретное приложение собирает движок из модулей и сервисов. В `EngineBase` эту роль выполняют smoke apps или support-layer, а не нижние модули.

### EngineBase
Нижний стабильный слой движка. Содержит foundation primitives, memory baseline, diagnostics, core lifecycle, platform/window, input, минимальный RHI и D3D11 clear-screen backend. Не содержит ресурсы, мир, gameplay, renderer, physics, save/load или scripting.

### EngineBaseSupport
Официальный support/composition target для сборки `EngineBase` в приложениях. Может знать все модули базы, но сам не является нижним engine layer. Нижние модули не должны зависеть от него.

### EngineRuntime
Следующий слой после `EngineBase`. Должен содержать ресурсы, assets, serialization, persistence, world/streaming, scene, renderer, physics, environment, simulation и другие крупные runtime-системы.

### Game
Финальный игровой слой с конкретными правилами, контентом, балансом, квестами, NPC, экономикой и composition root конкретной игры.

### GameFramework
Слой переиспользуемых gameplay-механик: actor, item, inventory, interaction, dialogue, quest, faction, vendor/trade, schedule, needs и другие системы, которые уже имеют игровой смысл.

### Module Boundary
Жесткая граница отдельной библиотеки/target-а. Публичные заголовки лежат в `include`, внутренняя реализация — в `src`. Внешний код не должен включать внутренние файлы другого модуля.

### Stable Runtime Base
Итоговое состояние `EngineBase`, пригодное для дальнейшей разработки верхних слоев без постоянного переписывания ядра.

### Support Layer
Композиционный слой, который связывает готовые модули. Он может знать несколько модулей сразу, но не должен превращаться в место реализации поведения движка.

### Target
Отдельная CMake-библиотека или executable. Каждый модуль базы должен быть отдельным target-ом, чтобы CMake физически фиксировал направление зависимостей.

## 2. Core, lifecycle и сервисы

### Bootstrap
Ранняя стадия запуска модуля. На ней подготавливаются зависимости, регистрируются базовые сервисы и проверяются условия запуска.

### Configuration Service
Минимальный сервис конфигурации для runtime name, worker count, memory tracking flag, RHI debug flag и размеров окна по умолчанию. Не является игровым config/VFS/resource system.

### Core
Microkernel-слой `EngineBase`. Отвечает за lifecycle, modules, services, event bus, task scheduler, main-thread dispatcher, frame loop и configuration. Не зависит от Platform, Input, RHI или D3D11.

### Dependency Graph
Граф зависимостей модулей. Нужен для запуска зависимостей раньше зависимых модулей и остановки в обратном порядке. Должен обнаруживать дубликаты, отсутствующие зависимости и циклы.

### Event Bus
Системный механизм передачи событий между нижними слоями. Поддерживает typed events, sync dispatch, queued dispatch, FIFO-drain и unsubscribe. Не является gameplay scripting/event system.

### FrameContext
Контекст текущего кадра: frame index, delta time, absolute time и связанные временные значения. Не содержит scene graph, gameplay state или renderer data.

### Frame Loop
Системный цикл кадра. Оркестрирует platform pump, input update, queued events, main-thread tasks, module tick, RHI frame boundary, present и end frame.

### Frame Phase
Именованный этап кадра: `BeginFrame`, `PumpPlatformEvents`, `UpdateInput`, `DrainEvents`, `RunScheduledMainThreadTasks`, `TickModules`, `RhiBeginFrame`, `RhiEndFrame`, `Present`, `EndFrame`.

### IMainThreadDispatcher
Официальная очередь задач, которые должны выполниться на main thread. Принимает задачи через `Post`, исполняет их FIFO через `Drain`, проверяет main thread ownership и не должен проглатывать исключения.

### Initialize
Стадия полноценной инициализации модуля после bootstrap. На ней модуль получает необходимые сервисы и готовится к работе.

### ITaskScheduler
Общий scheduler для фоновых CPU-задач. Должен предоставлять worker pool, task handle, wait/wait group, wait idle, shutdown, exception propagation и базовую диагностику.

### Lifecycle
Последовательность жизни приложения и модулей: `Bootstrap`, `Initialize`, `Run/Tick`, `Shutdown`. Нижние модули запускаются только через общий lifecycle.

### Main Thread
Главный поток приложения. Отвечает за lifecycle, window message pump, frame orchestration, input snapshot publication, event drain, main-thread dispatcher drain, module tick, RHI begin/end/present и swap chain resize.

### Module
Единица подключения в `Core` с id, dependencies, bootstrap, initialize, tick и shutdown. Модуль не должен самовольно управлять чужими модулями напрямую.

### ModuleId
Стабильный id модуля. Используется dependency graph-ом и registry для регистрации, поиска дубликатов и проверки зависимостей.

### Run
Стадия выполнения application loop. На ней приложение повторяет frame phases до stop request, ошибки инициализации или frame limit в тестах/smoke apps.

### Service Container
Контейнер долгоживущих runtime-сервисов. Хранит сервисы через `std::shared_ptr`, запрещает null registration и duplicate registration, предоставляет typed lookup.

### ServiceId
Стабильный id типа/интерфейса сервиса. Нужен для типизированной регистрации и поиска без строковых имен в base layer.

### Shutdown
Финальная стадия остановки. Должна идти в обратном порядке и корректно освобождать сервисы, модули, worker tasks, окна и RHI-объекты.

### Tick
Периодический вызов модуля внутри frame loop. В `EngineBase` это системный tick, а не gameplay update.

### Worker Thread
Фоновый поток, управляемый `ITaskScheduler`. Может выполнять generic CPU work, будущую подготовку ресурсов, simulation jobs и render data preparation, но не должен качать Win32 message pump, делать Present или работать с D3D11 immediate context напрямую.

## 3. Foundation, память, диагностика и тесты

### AllocationTag
Метка подсистемы для учета памяти: Core, Platform, Input, RHI, D3D11, Diagnostics, Tests, Unknown и будущие теги верхних слоев. Нужна для статистики и бюджетов.

### Counter
Числовая диагностическая метрика: frames, tasks, events, memory, RHI presents, frame time и другие наблюдаемые значения.

### Diagnostics
Слой логирования, счетчиков и profiling scopes. Должен помогать видеть startup/shutdown, frame timing, задачи, events и память, но не зависеть от gameplay или верхних систем.

### Error
Структурированное описание ошибки: code/category, message и optional context. Используется вместе с `Result<T>` для ожидаемых runtime failures.

### Foundation
Нижний набор базовых primitives: Result/Error, ids, handles, time primitives, path basics. Не зависит от других engine modules.

### Handle<T>
Generation handle вида index + generation. Нужен как стабильная ссылка на объект, управляемый движком, без раскрытия владения и raw pointer ownership.

### Logger
Интерфейс логирования с уровнями Trace, Debug, Info, Warning, Error, Fatal. Лог должен содержать module/category, message, timestamp и thread id/name.

### Memory Budget
Диагностический лимит памяти для allocation tag. На этапе базы помогает обнаружить превышение, но не обязан жестко запрещать allocation.

### MemoryTracker
Сервис учета allocated bytes, peak bytes, allocation count и per-tag statistics. В `EngineBase` должен быть доступен как `IMemoryTracker`.

### NameId
Id для именованных сущностей. Пустая строка должна давать invalid id, одинаковая строка — одинаковый id.

### Path
Базовый path primitive для нижнего слоя. Не является VFS-path и не должен использоваться как путь игровых ресурсов.

### PhysicalPath
Путь к физическому файлу/директории ОС. В будущем должен быть отделен от виртуального пути ресурсов.

### Profiling Scope
Легкий API измерения участка кода. Должен быть совместим с будущим подключением Tracy, но на этапе базы может писать данные в logger или internal collector.

### Regression Test
Тест, фиксирующий ранее найденные ошибки и архитектурные ограничения: duplicate services, invalid resize, task exception propagation, dependency violations и другие случаи.

### Result<T>
Тип результата операции, которая может штатно завершиться ошибкой. Подходит для window creation, RHI device creation, dynamic library loading, backend initialization и invalid external input.

### StringId
Стабильный id строки без owning string по умолчанию. Пустая строка должна давать invalid id.

### Time Primitives
Базовые типы времени: `TimePoint`, `Duration`, `FrameTime`, `FrameIndex`. Нужны, чтобы не смешивать raw float/double/ticks по всему коду.

### Unit Test
Изолированный тест конкретного модуля. Должен линковать только необходимые targets, чтобы не скрывать нарушения dependency direction.

### VirtualPath
Будущий виртуальный путь ресурса/asset-а. Не должен смешиваться с `PhysicalPath`.

## 4. Platform, input и RHI

### D3D11 Backend
Конкретная реализация RHI поверх Direct3D 11. Должна жить в отдельном target-е `RHI_D3D11` и не протекать в публичный API `RHI`.

### GamepadState
Будущее состояние gamepad input. В текущем базовом этапе может быть только заделом, без gameplay actions.

### Input
Слой низкоуровневого ввода. Превращает platform events в input events и per-frame snapshot. Не знает про gameplay-команды вроде Interact, Attack, Talk или OpenInventory.

### InputEvent
Событие ввода: key down/up, mouse move, mouse button, mouse wheel, focus/capture changes. Используется нижним input layer-ом, а не gameplay bindings.

### InputSnapshot
Стабильное состояние ввода на кадр: keys down, pressed/released this frame, mouse position, mouse delta, buttons, wheel delta, focus/capture state.

### IPlatformRuntime
Платформенный runtime-интерфейс. На Windows отвечает за process info, timers, message pump, dynamic library loading, error conversion и shutdown.

### IRhiCommandContext
Абстрактный контекст команд RHI. В базовом scope нужен для BeginFrame, Clear, EndFrame и связанных presentation-операций.

### IRhiDevice
Абстрактное GPU-устройство. В `EngineBase` это minimal presentation boundary, а не полноценный renderer-grade device.

### IRhiSwapChain
Абстракция swap chain. Отвечает за present и resize. `Resize(width, height)` должен требовать положительные размеры; minimized/zero-size состояние обрабатывается выше RHI.

### IWindow
Абстрактное окно платформы. Позволяет получить client size, close/resize/focus/minimize/restore state и native handle через контролируемый контракт.

### IWindowSystem
Сервис создания и управления окнами. На Windows реализуется через Win32, но публичный контракт не должен тянуть D3D11 или renderer.

### KeyboardState
Состояние клавиатуры внутри input snapshot: текущие нажатые клавиши, нажатые в этом кадре и отпущенные в этом кадре.

### MouseState
Состояние мыши: позиция, delta, кнопки, wheel delta, focus/capture-related state.

### NativeWindowHandle
Платформенный handle окна, например HWND на Windows. Используется backend-ом для swap chain creation, но не раскрывает всю platform implementation.

### Platform
Слой ОС и окна. На этом этапе Windows-only. Отвечает за Win32 runtime, window creation, message pump, timers, dynamic libraries и platform events.

### PlatformEvent
Низкоуровневое событие платформы: close requested, resize, focus changed, minimized, restored и другие window/system events.

### RHI
Render Hardware Interface. В `EngineBase` это минимальная абстракция presentation GPU boundary: device, command context, swap chain, clear, present, resize, validation и ошибки.

### RhiClearDesc
Описание clear operation: цвет и другие параметры очистки. Не является material, render pass или frame graph.

### RhiClearScreenApp
Smoke app, проверяющая создание окна, D3D11 runtime, swap chain, clear screen через RHI, resize и close. Не должна содержать renderer layer.

### RhiDeviceDesc
Описание создания RHI device: backend/debug flags и базовые параметры. Должно быть platform-independent.

### RhiSwapChainDesc
Описание swap chain: размеры, формат, vsync/debug-related параметры. Должно валидироваться до вызова backend-а.

### WindowSmokeApp
Smoke app, проверяющая создание окна, message pump и close handling без renderer/gameplay.

## 5. План базы и критерии готовности

### D3D11 backend step
Этап реализации конкретного backend-а `RHI_D3D11`: device creation, swap chain, render target, clear screen, present, resize и конвертация D3D11/DXGI ошибок в engine `Error`.

### Definition of Done
Финальный набор условий, по которым этап считается завершенным: targets разделены, зависимости не нарушены, tests проходят, smoke apps запускаются, contracts задокументированы, мертвый тестовый код удален.

### Diagnostics step
Этап добавления logger, profiling scopes, counters и startup/shutdown diagnostics. Должен дать наблюдаемость без зависимости от верхних слоев.

### Foundation step
Этап стабилизации primitives: `Result`, `Error`, ids, handles, time primitives и path basics. Это минимальный язык для остальных нижних модулей.

### Frame Loop step
Этап фиксации `FrameContext`, frame phases, main-thread responsibilities, stop request, frame limit и измеримых counters/scopes кадра.

### Freeze step
Этап заморозки `EngineBase`: после него в базу не добавляются resources, world, renderer, gameplay, save/load, physics, weather или scripting. Допустимы багфиксы, тесты и строго контролируемые уточнения контрактов.

### Input step
Этап реализации input contracts, platform event integration, per-frame input snapshot, focus/capture rules, tests и `InputSmokeApp`.

### Integration step
Этап проверки совместной работы Platform, Input, RHI, scheduler, event bus, diagnostics и frame loop. Должен подтвердить порядок запуска, shutdown и работу всех smoke apps.

### Memory step
Этап добавления memory tracking, allocation tags, per-tag statistics, budgets, over-budget detection и тестов без переписывания всего кода на custom containers.

### Microkernel step
Этап стабилизации `Core`: application lifecycle, module registry, service container, event bus, task scheduler, configuration и main-thread dispatch.

### Platform step
Этап реализации Windows runtime и window layer: process info, timers, message pump, dynamic libraries, Win32 window, native handle, resize/focus/minimize/restore events и smoke app.

### RHI step
Этап реализации минимального RHI abstraction: device, swap chain, command context, descriptors, BeginFrame, Clear, EndFrame, Present, Resize и Result/Error handling без renderer concepts.

### Structure step
Этап фиксации структуры `EngineBase`, отдельных CMake targets, public/private include boundaries, Apps, Tests и запрета зависимостей нижних слоев от верхних.

### Testing step
Этап усиления unit, integration и regression tests так, чтобы тесты не маскировали dependency violations и проверяли failure paths, lifecycle, dispatcher, scheduler, RHI validation и resize/minimize contract.

---

# II. Толковый словарь терминологии runtime

## 6. Runtime-архитектура и правила взаимодействия

### Command Queue
Очередь команд, через которую один major может запросить действие у другого без прямого управления его внутренним состоянием.

### Contract
Документированная граница обмена между majors: интерфейс, projection, event, command/effect queue или support composition. Прямое командование одного major другим запрещено.

### Effect Buffer
Буфер предложенных изменений мира или состояния. Позволяет собрать эффекты разных систем и применить их в контролируемом порядке через resolver.

### Effect Resolver
Механизм упорядоченного применения эффектов. Нужен, чтобы simulation, world, persistence и gameplay не меняли authoritative state хаотично.

### EngineRuntime Support
Composition layer для runtime majors. Должен появляться в конце, когда отдельные majors уже готовы и протестированы. Не содержит поведения, логики мира или storage.

### Major
Крупная runtime-система: Assets, Resources, Persistence, World, Streaming, Renderer, Physics, Simulation и другие. Major должен быть модульным и не должен превращаться в God module.

### Projection
Снимок или проекция данных одного major для другого. Например, Environment дает renderer-у surface/weather projection, но не командует renderer-ом напрямую.

### Runtime Service
Долгоживущий сервис `EngineRuntime`, зарегистрированный в composition layer и доступный другим системам только через контракт.

### Runtime Tick
Логический тик runtime-системы. Не обязан совпадать с render frame; может использоваться для simulation, streaming, time и scheduled events.

### Shared Runtime State
Общее состояние runtime-слоя, доступное через контролируемые registry, stores, projections и events, а не через глобальные объекты.

## 7. Идентичность, состояние и persistence

### AbstractFact
Самый дешевый уровень существования объекта или события. Объект не материализован, а представлен фактом: “фермер доставил картошку”, “предмет находится в контейнере”, “рынок продал часть товара”.

### AssetId
Стабильный runtime-id ассета. Ссылается на исходное контентное описание, а не на загруженные данные и не на конкретный объект мира.

### AsyncOperationStatus
Статус асинхронной операции: Pending, Running, WaitingForMainThread, PartiallyComplete, Completed, Failed, Cancelled.

### Base Placement
Базовое размещение объекта в мире из region/chunk/procedural data. Пока объект не изменен, save может не хранить его отдельной записью.

### ChunkId
Id технической единицы world streaming. Chunk нужен загрузке и выгрузке данных, но не обязан совпадать с simulation zone.

### Dirty State
Признак, что объект изменился относительно базового мира и должен быть сохранен как override или persistent record. Например, изменился transform, container, ownership, runtime state или объект уничтожен.

### ObjectRealityLevel
Уровень реальности объекта: Physical, Logical, AbstractFact. Помогает деградировать детализацию мира без потери наблюдаемой согласованности.

### PersistenceTier
Уровень важности сохранения: Disposable, TemporaryObserved, PlayerTouched, Protected, QuestCritical.

### Persistent Object
Runtime instance, состояние которого должно переживать streaming и save/load. Если игрок или важная система изменила объект, он получает persistent record.

### PersistentObjectId
Стабильный id persistent object-а. Переживает streaming, save/load и перезагрузку runtime-сессии.

### RegionId
Id крупной логической области мира. Region может содержать много chunks и simulation zones.

### ResidencyState
Состояние присутствия данных/объекта в runtime: Unloaded, Loading, Resident, Active, Sleeping, Unloading.

### ResourceId
Id загруженного или загружаемого runtime-представления asset-а. Не равен asset id и не является object id.

### RuntimeObjectId
Id конкретного объекта в текущей runtime-сессии. Может исчезнуть после выгрузки или перезагрузки, если объект не persistent.

### Save Override
Запись в сохранении, которая переопределяет базовый мир. Например: “картошка с placement id X больше не на столе, а в контейнере NPC Y”.

### SimulationLod
Уровень детализации симуляции: от полной физики/AI рядом с игроком до агрегированных фактов или time-skip далеко от игрока.

### SimulationZoneId
Id логической зоны симуляции. Simulation zone нужна логике мира и не обязана совпадать с chunk-границами.

### SurfaceId
Id поверхности или участка поверхности, для которого можно хранить wetness, snow, mud, ice, footprints, disturbance и другие surface states.

### Tombstone
Запись о том, что объект был уничтожен или удален. Нужна, чтобы при повторной загрузке базового мира объект не появился снова.

## 8. Контент, данные и загрузка

### ArchiveReader
Низкоуровневый API чтения структурированных данных в Serialization. Не знает про save slot, world или gameplay.

### ArchiveWriter
Низкоуровневый API записи структурированных данных. Должен учитывать schema/version metadata и стабильность формата.

### Asset
Исходное описание чего-либо в проекте: файл, запись или пакетный элемент. Например, model, texture, item definition, actor definition, region data. Asset — это “что это такое по данным проекта”.

### AssetCatalog
Индекс известных assets. Позволяет найти asset, его тип, metadata, location, зависимости, hash/version и tags.

### AssetDependencyManifest
Описание зависимостей asset-а от других assets. Используется для валидации и будущей загрузки ресурсов.

### AssetLocationResolver
Компонент, который определяет, где физически или пакетно находится asset.

### AssetMetadata
Метаданные asset-а: тип, версия, hash, dependencies, tags, source location и package/mount information.

### AssetPackageRegistry
Будущая точка учета pak/archive/package/mount-форматов. Не должен сам управлять lifetime загруженных ресурсов.

### AssetState
Состояние asset-а в каталоге: Unknown, Discovered, Indexed, Validated, Missing, Invalid, Deprecated.

### FormatAdapter
Адаптер формата сериализации: binary, JSON, dev-readable format или другой стабильный формат.

### Migration
Преобразование старой версии данных к новой schema. Нужна для assets, persistence и долговременных форматов.

### Resource
Загруженное или загружаемое runtime-представление asset-а, пригодное для использования движком: mesh resource, texture resource, material resource, item definition resource, region data resource.

### ResourceCache
Хранилище загруженных ресурсов. Управляет reuse, lifetime и eviction, но не должен знать gameplay-смысл ресурса.

### ResourceDependencyGraph
Граф runtime-зависимостей между ресурсами. Нужен, чтобы ресурс не стал Ready до готовности зависимостей.

### ResourceHandle
Ссылка на ресурс, управляемый Resources. Не раскрывает владение и не является указателем на gameplay object.

### ResourceLoaderRegistry
Реестр загрузчиков по типам ресурсов. Связывает тип ресурса с loader-ом, но не должен превращаться в gameplay factory.

### ResourceManager
Главный public service модуля Resources. Обрабатывает request/acquire/release, async loading, cache, dependency loading, budgets и state queries.

### ResourceState
Состояние ресурса: Unloaded, Queued, Loading, WaitingForDependencies, Ready, Failed, Evicting, Evicted, Reloading.

### Runtime Instance
Конкретный объект в работающем мире. Например, не “картошка вообще”, а “эта картошка лежит на столе, имеет id, condition, owner и placement”.

### SchemaVersioning
Правила версионирования форматов данных. Нужны, чтобы старые records можно было прочитать или мигрировать.

### Serialization
Общий механизм чтения/записи структурированных данных. Не является save system и не решает, какие объекты мира нужно сохранять.

### SerializerRegistry
Реестр сериализаторов типов. Должен быть инфраструктурой форматов, а не местом gameplay-логики.

## 9. Мир, симуляция и представление

### Animation
Runtime-инфраструктура animation data и playback: skeletons, clips, animation instances, pose evaluation, blending, animation events и animation LOD. Не решает combat, movement rules или NPC behavior.

### Audio
Runtime-звук: audio device, emitters, listeners, spatialization, ambience, mixer groups, sound resources и audio events. Не должен владеть gameplay-смыслом событий.

### Environment
Runtime-состояние окружающей среды: weather, season, temperature, wind, precipitation, humidity, snow, ice, wetness и surface modifiers. Не командует renderer/physics напрямую, а дает projections.

### Navigation
Runtime-данные перемещения: navmesh/navgraph tiles, path queries, dynamic obstacles, area costs, traversal filters и nav streaming. Не решает, куда NPC хочет идти.

### Physics
Runtime-физика: bodies, shapes, collision queries, raycasts, contact events, impulses, sleeping/active state. Не является combat, damage или gameplay movement logic.

### PlacementSystem
Система положения объекта: world surface, container, equipped, inventory, hidden, destroyed. Дает миру понимание, где объект существует.

### Renderer
Runtime foundation визуального представления: render scene, render proxies, views/cameras, visibility, mesh/material handles, synchronization и связь с RHI. Не знает, что объект является картошкой, NPC или сундуком.

### RenderProxy
Визуальное представление объекта для renderer-а: transform, mesh/material resource handles, visibility flags, render layer и dirty state. Отделяет renderer от gameplay/world object internals.

### Scene
Spatial-слой runtime: transforms, scene nodes, spatial index, bounds, visibility input data, attach/detach, spatial queries и dirty transform tracking. Тоньше, чем World, и не является renderer-ом.

### Simulation
Runtime-инфраструктура симуляции: scheduling, budgets, attention/relevance scoring, simulation LOD, abstract facts, world memory, effect queues и resolvers. Не является NPC brain или gameplay AI.

### Streaming
Система переходов загрузки и активности: requests, priority, residency transitions, load/unload scheduling, prefetch, budgeting и attention-driven streaming. Не загружает ресурсы руками, а работает через Resources и World contracts.

### SurfaceState
Состояние поверхности: Dry, Wet, Muddy, SnowCovered, Icy, Frozen, Thawing, Drying. Может использоваться renderer, physics, audio, simulation и gameplay через projections.

### Time
Authoritative major игрового времени: game time, time scale, pause, time skip, calendar, day phase и time events. Не решает погоду, schedules или quest logic, но дает им общий источник времени.

### World
Логическая структура мира: regions, chunks, object registry, placement, residency, materialization/demotion, world queries и связь objects с местом существования. Не должен превращаться в God module.

### WorldObjectRegistry
Registry объектов мира. Связывает object id, reality level, placement, residency и persistence-related состояние без gameplay-смысла объекта.

### WorldQuery
Публичные запросы к миру: найти объект, placement, residency, region/chunk принадлежность или spatial/logical состояние через документированный контракт.
