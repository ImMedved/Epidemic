# Stable Runtime Base Development Plan

## 1. Общие положения

`Stable Runtime Base` — это нижний стабильный срез движка, на котором позже будут строиться runtime services, resource manager, renderer, simulation, gameplay framework, scripting и игровые модули.

Цель этапа — создать изолированную, проверяемую и контрактно стабильную базу:

* `Foundation`;
* `Memory / Allocators Baseline`;
* `Microkernel / Core`;
* `Diagnostics / Profiling Baseline`;
* `Frame Loop / Frame Phases`;
* `Platform / Windows / Window`;
* `Input`;
* `RHI`;
* `RHI.D3D11`.

Этот этап не содержит работы с файлами игры, скриптами, ресурсами, текстурами, моделями, gameplay, NPC, мирами, квестами, UI, сохранениями или симуляцией.

Нижние слои предоставляют механизмы, но не знают о смыслах будущей игры. После завершения этого этапа база должна быть достаточно стабильной, чтобы верхние системы можно было разрабатывать поверх нее без постоянного изменения ядра.

## 2. Общее описание архитектуры

Архитектура строится как набор отдельных библиотек. Каждый модуль лежит в отдельной папке, имеет свой CMake target, публичные контракты и внутреннюю реализацию.

Базовые модули:

```text
EngineBase/
  Foundation/
  Memory/
  Core/
  Diagnostics/
  Platform/
  Input/
  RHI/
  RHI_D3D11/
  Apps/
  Tests/
```

Направление зависимостей строго одностороннее:

```text
Foundation
  <- Memory
  <- Diagnostics
  <- Core
  <- Platform
  <- Input
  <- RHI
  <- RHI.D3D11
  <- Apps
```

`Core` не знает о `Platform`, `Input`, `RHI`, `D3D11`, renderer, gameplay, simulation, resources или scripts.

`Platform` не знает о renderer, gameplay, input actions или ресурсах.

`Input` не знает о gameplay-командах вроде `Interact`, `Attack`, `Talk`, `OpenInventory`.

`RHI` не знает о renderer, materials, meshes, scene graph, UI или gameplay.

`RHI.D3D11` знает только о RHI-контрактах и минимальном native window handle, который нужен для создания swap chain.

Конкретное приложение в `Apps/*` является composition root. Оно решает, какие модули подключить, какие реализации сервисов зарегистрировать и какой smoke/demo сценарий запустить.

## 3. Взаимодействие Stable Runtime Base с остальным кодом игры

Будущие верхние слои будут подключаться к `Stable Runtime Base` как отдельные библиотеки:

```text
Runtime Services
Resource Manager
Renderer
Simulation
Gameplay Framework
Scripting
Game Modules
```

Эти слои могут использовать `Stable Runtime Base`, но `Stable Runtime Base` не должен зависеть от них.

Пример будущего подключения:

```cpp
int main()
{
    Application app;

    RegisterFoundation(app);
    RegisterMemory(app);
    RegisterDiagnostics(app);
    RegisterCore(app);

    RegisterWindowsPlatform(app);
    RegisterInput(app);
    RegisterRhi(app);
    RegisterD3D11Backend(app);

    // Later:
    // RegisterRuntimeServices(app);
    // RegisterResourceManager(app);
    // RegisterRenderer(app);
    // RegisterSimulation(app);
    // RegisterGameplay(app);

    return app.Run();
}
```

`Core` не должен содержать прямых вызовов вида:

```cpp
RegisterWindowsPlatform();
RegisterD3D11();
RegisterGameplay();
```

Это делает только приложение или специальный composition module.

### 3.1. Как верхние модули будут подключаться

Каждый следующий модуль будет отдельной библиотекой с публичным интерфейсом. Он сможет зарегистрировать свои сервисы и lifecycle-модуль в application composition root.

Пример:

```text
RendererModule depends on RHI + ResourceManager later
SimulationModule depends on Core + TaskScheduler + World contracts later
GameplayModule depends on Simulation + Input snapshot + Event system later
```

Нижние модули не должны знать о существовании этих верхних модулей.

### 3.2. Как все будет работать эффективно и распределенно

Большой проект нельзя строить вокруг одного потока. Но базовый слой не должен превращаться в хаотичный набор потоков.

Правильная модель:

```text
Main Thread:
  application lifecycle
  window message pump
  frame orchestration
  input snapshot update
  RHI frame boundary
  present

Worker Threads:
  generic CPU tasks
  future resource preparation
  future simulation chunks
  future AI planning
  future render data preparation
```

Ни один subsystem не должен самостоятельно создавать долгоживущие `std::thread` без явного основания. Все обычные фоновые CPU-задачи должны идти через общий `ITaskScheduler`.

На этом этапе нужен не финальный AAA job system, а стабильная основа:

```text
worker pool
task queue
task handle
task group / wait group
wait
wait idle
exception propagation
shutdown
thread naming
basic diagnostics
```

Позже поверх этого можно будет строить:

* streaming scheduler;
* simulation jobs;
* AI planning jobs;
* render preparation jobs;
* background resource decode;
* profiling and task visualization.

### 3.3. Как нижние слои сообщают данные наверх

Нижние слои не вызывают верхние напрямую.

Допустимые способы передачи данных:

* return value;
* service interface;
* event queue;
* per-frame snapshot;
* callback/observer interface, объявленный в нижнем слое;
* polling API.

Примеры platform events:

```text
WindowCloseRequested
WindowResized
WindowFocusChanged
WindowMinimized
WindowRestored
```

Примеры input events:

```text
KeyPressed
KeyReleased
MouseMoved
MouseButtonPressed
MouseButtonReleased
MouseWheel
```

Gameplay actions не входят в input layer. Они появятся позже в gameplay input binding layer.

## 4. Структура проекта

Весь текущий базовый срез должен лежать внутри папки `EngineBase`.

Целевая структура:

```text
EngineBase/
  Foundation/
    include/
      Epidemic/Foundation/
    src/
    tests/

  Memory/
    include/
      Epidemic/Memory/
    src/
    tests/

  Diagnostics/
    include/
      Epidemic/Diagnostics/
    src/
    tests/

  Core/
    include/
      Epidemic/Core/
    src/
    tests/

  Platform/
    include/
      Epidemic/Platform/
    src/
      Windows/
    tests/

  Input/
    include/
      Epidemic/Input/
    src/
    tests/

  RHI/
    include/
      Epidemic/RHI/
    src/
    tests/

  RHI_D3D11/
    include/
      Epidemic/RHI_D3D11/
    src/
    tests/

  Apps/
    HeadlessCoreApp/
    WindowSmokeApp/
    InputSmokeApp/
    RhiClearScreenApp/

  Tests/
    Integration/
    Regression/

  CMakeLists.txt
```

Каждый модуль должен быть отдельной библиотекой:

```text
EpidemicFoundation
EpidemicMemory
EpidemicDiagnostics
EpidemicCore
EpidemicPlatform
EpidemicInput
EpidemicRHI
EpidemicRHI_D3D11
```

Приложения должны быть отдельными executable targets:

```text
EpidemicHeadlessCoreApp
EpidemicWindowSmokeApp
EpidemicInputSmokeApp
EpidemicRhiClearScreenApp
```

Публичные заголовки лежат в `include/`.

Внутренняя реализация лежит в `src/`.

Внешний код не должен include-ить внутренние файлы другого модуля из `src/`.

Допустимо:

```cpp
#include <Epidemic/RHI/Device.hpp>
```

Недопустимо:

```cpp
#include "../../RHI_D3D11/src/D3D11Device.hpp"
```

## 5. План разработки

## Шаг 1. Зафиксировать структуру `EngineBase`

### Пункт 1. Создать или привести в порядок папку `EngineBase`

Весь стабильный срез должен находиться внутри `EngineBase`.

Нужно получить структуру:

```text
EngineBase/Foundation
EngineBase/Memory
EngineBase/Diagnostics
EngineBase/Core
EngineBase/Platform
EngineBase/Input
EngineBase/RHI
EngineBase/RHI_D3D11
EngineBase/Apps
EngineBase/Tests
```

Если код уже существует в другой структуре, его нужно перенести без изменения смысла.

### Пункт 2. Разделить модули на отдельные библиотеки

Каждый слой должен стать отдельным CMake target.

Нужно создать targets:

```text
EpidemicFoundation
EpidemicMemory
EpidemicDiagnostics
EpidemicCore
EpidemicPlatform
EpidemicInput
EpidemicRHI
EpidemicRHI_D3D11
```

CMake должен физически фиксировать направление зависимостей. Если `Core` случайно начнет зависеть от `Platform`, это должно стать заметно на этапе сборки.

### Пункт 3. Настроить public/private include boundaries

В каждом модуле нужно разделить публичные заголовки и внутреннюю реализацию.

Публичные заголовки:

```text
EngineBase/<Module>/include/Epidemic/<Module>/
```

Внутренняя реализация:

```text
EngineBase/<Module>/src/
```

### Пункт 4. Обновить tests и smoke apps

После изменения структуры нужно убедиться, что:

* unit tests собираются;
* integration tests собираются;
* regression tests собираются;
* smoke apps собираются;
* CTest видит тесты;
* старые include paths не используются.

### Пункт 5. Definition of Done

Шаг завершен, если:

* весь стабильный срез лежит в `EngineBase`;
* каждый модуль является отдельной библиотекой;
* CMake-сборка работает;
* CTest запускает тесты;
* `Core` не зависит от `Platform`, `Input`, `RHI`, `RHI_D3D11`;
* `Foundation` не зависит ни от одного engine layer.

## Шаг 2. Довести Foundation до стабильного минимального состояния

### Пункт 1. `Result` / `Error`

Нужно иметь единый механизм ожидаемых ошибок.

`Result<T>` должен использоваться в операциях, которые могут штатно завершиться ошибкой:

* module initialization;
* service registration;
* platform operations;
* input initialization;
* RHI creation;
* D3D11 initialization.

`Error` должен хранить:

* error code или category;
* human-readable message;
* optional source/context.

### Пункт 2. Id helpers

Нужно иметь стабильные типы:

```text
StringId
NameId
ModuleId
ServiceId
EventTypeId
```

Пустая строка должна давать invalid id.

Одинаковая строка должна давать одинаковый id.

Id-типы не должны хранить owning string по умолчанию.

### Пункт 3. Handles

Нужен базовый generation handle:

```text
Handle<T>
index + generation
invalid handle
validity check
comparison
hash support
```

На этом этапе handle не является resource manager. Это только foundation primitive.

### Пункт 4. Time primitives

Нужно ввести базовые типы времени:

```text
TimePoint
Duration
FrameTime
FrameIndex
```

Они понадобятся platform, input, scheduler, frame loop и diagnostics.

Не нужно смешивать raw `float deltaTime`, `double seconds` и `uint64_t ticks` по всему коду.

### Пункт 5. Path basics

На этом этапе нужен только базовый path primitive.

Важно не смешивать будущие типы:

```text
PhysicalPath
VirtualPath
```

Если полноценное разделение еще рано, нужно зафиксировать, что текущий `Path` не является VFS-path и не должен использоваться для игровых ресурсов.

### Пункт 6. Tests

Нужно покрыть:

* `Result` success/failure;
* `Error` message/context;
* empty `StringId` invalid;
* empty `NameId` invalid;
* stable id generation;
* handle invalid/default state;
* handle equality;
* time conversion;
* path normalization basics.

### Пункт 7. Definition of Done

Foundation готов, если:

* перечисленные primitives реализованы;
* primitives покрыты тестами;
* `Foundation` не зависит от других engine modules;
* остальные base modules используют Foundation primitives вместо локальных дубликатов.

## Шаг 3. Добавить Memory / Allocators Baseline

### Пункт 1. Memory tracking

Нужно добавить базовый memory tracking, который позволит понимать, какие подсистемы потребляют память.

На этом этапе не нужен сложный memory manager. Нужен минимальный слой наблюдаемости.

Минимальные возможности:

```text
allocation tags
allocated bytes counter
peak allocated bytes
allocation count
per-tag statistics
debug-only tracking mode
```

### Пункт 2. Allocation tags

Нужно завести стандартные allocation tags:

```text
Core
Platform
Input
RHI
D3D11
Diagnostics
Tests
Unknown
```

Позже верхние слои смогут добавить:

```text
Renderer
Resources
Simulation
Gameplay
AI
```

Но сейчас эти теги не должны тянуть сами верхние системы.

### Пункт 3. Memory budget interface

Нужно предусмотреть простой интерфейс бюджетов:

```text
SetBudget(tag, bytes)
GetBudget(tag)
GetUsage(tag)
IsOverBudget(tag)
```

На этом этапе budget может быть диагностическим, а не жестко ограничивающим. Он должен помогать видеть проблему, но не обязан запрещать allocation.

### Пункт 4. Allocator baseline

Не нужно сразу писать сложные кастомные allocators.

Допустимо:

* tracking allocator для debug/test;
* frame allocator interface placeholder;
* no-op default allocator wrapper;
* helper для tagged allocation, если это не ломает стандартные контейнеры.

Не нужно переписывать весь код на кастомные контейнеры.

### Пункт 5. Tests

Нужно покрыть:

* allocation tag accounting;
* peak memory accounting;
* budget set/get;
* over-budget detection;
* unknown tag behavior;
* reset statistics in tests.

### Пункт 6. Definition of Done

Memory baseline готов, если:

* можно увидеть memory usage по tags;
* можно задать диагностический budget;
* есть тесты;
* нет зависимости от renderer/resources/gameplay;
* код не требует переписывания всех стандартных контейнеров.

## Шаг 4. Довести Microkernel / Core до стабильного состояния

### Пункт 1. Application lifecycle

`Core` владеет жизненным циклом приложения:

```text
Bootstrap
Initialize
Run / Tick
Shutdown
```

`Core` не знает, какие конкретные модули подключены.

`Application` принимает регистрацию модулей и сервисов извне.

### Пункт 2. Module system

Нужно иметь общий интерфейс модуля:

```text
Id
Dependencies
Bootstrap
Initialize
Tick
Shutdown
```

Dependency graph должен поддерживать:

* запуск зависимостей раньше зависимых модулей;
* shutdown в обратном порядке;
* duplicate module id detection;
* missing dependency detection;
* circular dependency detection;
* запрет регистрации после старта lifecycle.

### Пункт 3. Service container

Service container должен поддерживать:

* interface-based registration;
* typed lookup;
* `std::shared_ptr` ownership;
* null registration forbidden;
* duplicate registration forbidden;
* contains;
* controlled error path for missing service;
* no string service names в base layer.

### Пункт 4. Event bus

Event bus нужен для системных событий и безопасной передачи сообщений между слоями.

Минимальные требования:

* typed events;
* sync dispatch;
* queued dispatch;
* FIFO baseline for queued events;
* drain queued events;
* unsubscribe;
* empty handler registration forbidden.

Event bus не должен становиться gameplay event scripting system.

### Пункт 5. Task scheduler

`Core` должен предоставлять общий scheduler.

Минимальные требования:

```text
worker pool
Schedule(task)
TaskHandle
TaskGroup / WaitGroup
Wait(handle)
Wait(group)
WaitIdle()
exception propagation
shutdown
thread naming
worker count configuration
basic task diagnostics
```

Исключения в задачах нельзя проглатывать. Если task падает, ошибка должна быть видна через `Wait`, `WaitIdle` или `Result`.

### Пункт 6. Configuration

Нужен минимальный configuration service.

Он должен поддерживать:

* set/get values;
* basic typed access;
* runtime name;
* worker count;
* memory tracking flag;
* RHI debug flag;
* default window width/height.

Не нужно читать игровые конфиги, ini-файлы или resource configs.

### Пункт 7. Tests

Нужно покрыть:

* application lifecycle;
* module startup order;
* reverse shutdown;
* duplicate module id;
* missing dependency;
* circular dependency;
* service registration;
* duplicate service;
* missing service;
* sync event dispatch;
* queued event dispatch;
* unsubscribe;
* scheduler task execution;
* scheduler exception propagation;
* scheduler shutdown.

### Пункт 8. Definition of Done

Core готов, если:

* он не зависит от Platform/Input/RHI/D3D11;
* все модули запускаются только через общий lifecycle;
* сервисы регистрируются извне;
* scheduler работает с worker pool;
* event bus имеет sync/queued режимы;
* lifecycle, services, events и scheduler покрыты тестами.

## Шаг 5. Добавить Diagnostics / Profiling Baseline

### Пункт 1. Logging

Нужно иметь базовый logger interface.

Минимальные уровни:

```text
Trace
Debug
Info
Warning
Error
Fatal
```

Логи должны уметь выводить:

* имя модуля;
* категорию;
* сообщение;
* thread id или thread name;
* timestamp.

### Пункт 2. Profiling scopes

Нужно добавить легкий profiling scope API.

Примерно:

```cpp
EPIDEMIC_PROFILE_SCOPE("TaskScheduler::WaitIdle");
EPIDEMIC_PROFILE_SCOPE("Platform::PumpEvents");
EPIDEMIC_PROFILE_SCOPE("RHI::Present");
```

На этом этапе scope может писать данные только в debug logger или internal collector.

Важно: API должен быть совместим с будущим подключением Tracy, но Tracy не обязательно подключать сейчас.

### Пункт 3. Counters

Нужны базовые counters:

```text
frames
tasks scheduled
tasks completed
queued events
memory used
worker count
frame time
```

Эти counters должны быть доступны diagnostics layer, но не должны тянуть gameplay.

### Пункт 4. Startup/shutdown diagnostics

При запуске smoke apps должно быть видно:

* какие модули зарегистрированы;
* порядок bootstrap;
* порядок initialize;
* количество worker threads;
* memory tracking enabled/disabled;
* window creation info;
* RHI backend info;
* shutdown order.

### Пункт 5. Tests

Нужно покрыть:

* logger receives message;
* profiling scope records duration or event;
* counters increment/decrement;
* diagnostics do not crash when disabled;
* diagnostics do not depend on upper layers.

### Пункт 6. Definition of Done

Diagnostics baseline готов, если:

* есть logger;
* есть profiling scope API;
* есть counters;
* startup/shutdown видны в логах;
* API можно позже связать с Tracy;
* нет зависимости от renderer/resources/gameplay.

## Шаг 6. Добавить Frame Loop / Frame Phases

### Пункт 1. Frame context

Нужно ввести базовый `FrameContext`.

Он должен содержать:

```text
frame index
delta time
absolute time
raw delta time
smoothed delta time later, если нужно
```

На этом этапе `FrameContext` не содержит gameplay-состояния, scene graph или renderer data.

### Пункт 2. Frame phases

Нужно явно зафиксировать порядок кадра.

Минимальные фазы:

```text
BeginFrame
PumpPlatformEvents
UpdateInput
DrainEvents
RunScheduledMainThreadTasks
TickModules
RhiBeginFrame
RhiEndFrame
Present
EndFrame
```

Это пока не gameplay loop. Это системный frame loop, на который позже смогут опираться renderer, simulation и gameplay.

### Пункт 3. Main thread ownership

Нужно явно определить, что выполняется на main thread:

```text
window message pump
input snapshot publication
frame phase orchestration
RHI present
module tick entry points
```

Worker threads выполняют только задачи через scheduler.

### Пункт 4. Shutdown condition

Frame loop должен уметь завершаться по системному сигналу:

```text
window close requested
application stop requested
fatal initialization error
test/smoke app frame limit
```

### Пункт 5. Frame diagnostics

Каждый кадр должен быть измерим:

```text
frame index
frame time
platform pump time
input update time
module tick time
RHI present time
scheduled tasks count
```

Не обязательно выводить это каждый кадр в лог, но counters/scopes должны существовать.

### Пункт 6. Tests

Нужно покрыть:

* frame index increments;
* frame context contains valid delta;
* phases execute in expected order;
* stop request exits loop;
* frame limit exits loop in tests;
* modules tick inside frame loop.

### Пункт 7. Definition of Done

Frame loop готов, если:

* есть явные frame phases;
* есть `FrameContext`;
* main thread responsibilities зафиксированы;
* worker tasks не смешаны с frame orchestration;
* tests подтверждают порядок фаз.

## Шаг 7. Реализовать Platform / Windows / Window layer

### Пункт 1. Platform contracts

Platform module должен предоставить платформенно-независимые контракты:

```text
IPlatformRuntime
IWindow
IWindowSystem
NativeWindowHandle
PlatformEvent
```

Эти контракты не должны зависеть от D3D11, RHI или gameplay.

### Пункт 2. Windows runtime implementation

Windows implementation должна жить отдельно:

```text
EngineBase/Platform/src/Windows/
```

Она может использовать Win32 API.

Она должна реализовать:

* process info;
* high-resolution timer;
* message pump;
* dynamic library loading;
* symbol lookup;
* error conversion from Win32 error codes;
* clean shutdown.

### Пункт 3. Window creation

Нужно реализовать создание окна через Win32.

Минимальные требования:

* create window;
* show window;
* close requested event;
* resize event;
* focus event;
* minimized/restored event;
* native HWND extraction through `NativeWindowHandle`;
* controlled destruction.

Window layer не должен знать о D3D11.

### Пункт 4. Window message pump

Platform должна иметь метод обработки системных сообщений.

Например:

```text
PumpEvents()
```

`PumpEvents()` должен собирать platform events и передавать их наружу через очередь или event bus.

Platform не должна напрямую вызывать gameplay, renderer или input actions.

### Пункт 5. DPI and monitor basics

Нужны только базовые вещи:

* узнать DPI окна;
* узнать размер client area;
* корректно обработать resize.

Не нужно делать полноценный multi-monitor manager, fullscreen modes или advanced DPI policy.

### Пункт 6. Platform tests

Нужно покрыть:

* process info exists;
* monotonic clock increases;
* dynamic library load failure returns meaningful error;
* missing symbol returns meaningful error;
* platform module registration works.

Window smoke test лучше вынести в отдельное приложение.

### Пункт 7. Definition of Done

Platform layer готов, если:

* есть Windows implementation;
* можно создать окно;
* можно получить native window handle;
* можно обрабатывать close/resize/focus events;
* слой не зависит от Input/RHI/D3D11/Renderer/Gameplay;
* есть smoke app `WindowSmokeApp`.

## Шаг 8. Реализовать Input layer

### Пункт 1. Input contracts

Input layer должен предоставить платформенно-независимые контракты:

```text
IInputSystem
InputEvent
InputSnapshot
KeyboardState
MouseState
GamepadState later
```

Input не должен знать о gameplay actions.

### Пункт 2. Platform event integration

Input layer должен принимать низкоуровневые события от Platform:

```text
key down
key up
mouse move
mouse button down/up
mouse wheel
window focus lost
```

Input обновляет внутреннее состояние и публикует input events.

### Пункт 3. Per-frame input snapshot

Каждый кадр input layer формирует snapshot:

```text
keys currently down
keys pressed this frame
keys released this frame
mouse position
mouse delta
mouse buttons
wheel delta
focus/capture state
```

Snapshot нужен для того, чтобы верхние слои читали стабильное состояние ввода в рамках кадра.

### Пункт 4. Focus and capture rules

Нужно корректно обработать:

* потерю фокуса;
* сброс pressed keys при потере фокуса;
* mouse capture, если оно нужно для future camera;
* cursor visibility на базовом уровне.

### Пункт 5. Tests

Нужно покрыть:

* key press changes state;
* key release changes state;
* pressed-this-frame works;
* released-this-frame works;
* mouse move updates position and delta;
* focus lost clears transient state;
* snapshot remains stable during frame.

### Пункт 6. InputSmokeApp

Нужно создать smoke app, которая:

* открывает окно;
* показывает в логах input events;
* закрывается по close event;
* может закрываться по Escape.

### Пункт 7. Definition of Done

Input layer готов, если:

* input отделен от platform implementation;
* gameplay actions отсутствуют;
* snapshot работает;
* input events работают;
* tests проходят;
* есть `InputSmokeApp`.

## Шаг 9. Реализовать RHI abstraction

### Пункт 1. RHI contracts

RHI должен описывать абстрактную GPU boundary.

Минимальные интерфейсы:

```text
IRhiDevice
IRhiSwapChain
IRhiCommandContext
IRhiResource
IRhiBuffer
IRhiTexture
```

На этом этапе нельзя писать renderer. RHI не знает о materials, meshes, scene, camera, UI или lights.

### Пункт 2. RHI descriptors

Нужно ввести описатели:

```text
RhiDeviceDesc
RhiSwapChainDesc
RhiBufferDesc
RhiTextureDesc
RhiClearDesc
RhiViewport
RhiScissorRect
```

Они должны быть platform-independent.

### Пункт 3. Minimal frame operations

Минимальные операции:

```text
BeginFrame
Clear
EndFrame
Present
ResizeSwapChain
```

Этого достаточно для clear-screen sample.

### Пункт 4. Resource handles

RHI должен возвращать typed handles или интерфейсные объекты.

RHI handles — это GPU-level objects, а не игровые ресурсы.

### Пункт 5. Error handling

Все RHI operations, которые могут упасть, должны возвращать `Result`.

Ошибки должны содержать:

* operation name;
* backend name, если есть;
* native error code, если есть;
* human-readable message.

### Пункт 6. Tests

Unit-тесты RHI abstraction проверяют:

* descriptor defaults;
* invalid descriptor validation;
* interface contracts;
* fake/null RHI backend, если нужен.

Настоящее GPU-поведение проверяется smoke app.

### Пункт 7. Definition of Done

RHI готов для D3D11 backend, если:

* есть абстрактные контракты device/swapchain/context;
* есть descriptors;
* есть minimal frame API;
* нет зависимости от D3D11 в RHI target;
* нет renderer/gameplay/resource concepts.

## Шаг 10. Реализовать RHI.D3D11 backend

### Пункт 1. D3D11 module boundary

D3D11 backend должен быть отдельной библиотекой:

```text
EngineBase/RHI_D3D11
```

Он зависит от:

```text
Foundation
Memory
Diagnostics
Core contracts
Platform native window contract
RHI
Windows SDK
D3D11
DXGI
```

`RHI` не зависит от `RHI_D3D11`.

### Пункт 2. Device creation

Нужно реализовать:

* adapter selection default;
* D3D11 device creation;
* immediate context;
* debug layer flag from configuration;
* feature level selection;
* meaningful error reporting.

### Пункт 3. Swap chain creation

Нужно создать swap chain для Win32 window.

Нужно использовать `NativeWindowHandle`, а не прямую зависимость от всей platform implementation.

Минимальные требования:

* create swap chain;
* create render target view;
* handle resize;
* release/recreate backbuffer resources;
* present.

### Пункт 4. Clear screen

Нужно реализовать clear operation через RHI interface.

Clear-screen sample должен идти через RHI abstraction, а не напрямую через D3D11 calls из app.

### Пункт 5. Resize handling

При resize окна D3D11 backend должен:

* получить новый client size;
* освободить dependent backbuffer resources;
* resize swap chain buffers;
* пересоздать render target;
* продолжить rendering.

Minimized window не должен приводить к падению.

### Пункт 6. Device error reporting

Ошибки D3D11/DXGI должны конвертироваться в engine `Error`.

Сообщение должно содержать:

* HRESULT;
* operation;
* backend name;
* readable text, если возможно.

### Пункт 7. RhiClearScreenApp

Нужно создать приложение:

```text
EngineBase/Apps/RhiClearScreenApp
```

Оно должно:

* создать application;
* зарегистрировать platform;
* создать окно;
* зарегистрировать RHI;
* зарегистрировать D3D11 backend;
* каждый кадр очищать экран цветом;
* обрабатывать resize;
* закрываться по window close;
* не использовать renderer layer.

### Пункт 8. Definition of Done

D3D11 backend готов, если:

* окно открывается;
* D3D11 device создается;
* swap chain создается;
* экран очищается цветом через RHI;
* resize работает;
* close работает;
* RHI не зависит от D3D11;
* renderer/gameplay/resources не добавлены.

## Шаг 11. Интеграция Stable Runtime Base

### Пункт 1. Общий application loop

После Platform, Input и RHI нужно иметь минимальный frame loop:

```text
BeginFrame
PumpPlatformEvents
UpdateInput
DrainEvents
RunScheduledMainThreadTasks
TickModules
RhiBeginFrame
Clear screen in smoke app
RhiEndFrame
Present
EndFrame
```

Это пока не gameplay loop.

### Пункт 2. Module dependency graph

Нужно проверить порядок:

```text
Foundation available first
Memory initialized
Diagnostics initialized
Core services available
Platform initialized
Input initialized after Platform
RHI initialized
D3D11 backend initialized after Platform + RHI
Smoke app module initialized last
```

Shutdown должен идти в обратном порядке.

### Пункт 3. Threading validation

Нужно убедиться, что:

* main thread отвечает за window pump;
* worker tasks выполняются параллельно;
* scheduler корректно завершается;
* RHI calls выполняются в контролируемом месте;
* нет хаотичных subsystem-owned threads.

### Пункт 4. Diagnostics output

При запуске smoke apps должно быть видно:

* какие модули зарегистрированы;
* порядок bootstrap;
* порядок initialize;
* количество worker threads;
* memory usage baseline;
* window creation info;
* RHI backend info;
* frame timing;
* shutdown order.

### Пункт 5. Definition of Done

Интеграция завершена, если:

* `HeadlessCoreApp` работает;
* `WindowSmokeApp` работает;
* `InputSmokeApp` работает;
* `RhiClearScreenApp` работает;
* все tests проходят;
* dependency rules не нарушены.

## Шаг 12. Тестирование, стабилизация и заморозка контрактов

### Пункт 1. Unit tests

Нужно покрыть:

```text
Foundation primitives
Memory tracking
Diagnostics counters/scopes
ServiceContainer
ModuleRegistry
EventBus
TaskScheduler
FrameLoop
InputSnapshot
RHI descriptor validation
```

### Пункт 2. Integration tests

Нужно покрыть:

```text
application lifecycle
module startup/shutdown order
platform module registration
input module with synthetic events
scheduler integration
event bus integration
frame phase order
```

### Пункт 3. Regression tests

Нужно покрыть:

```text
duplicate service registration
null service registration
missing service
duplicate module id
missing dependency
circular dependency
register module after start
empty event handler
unsubscribe unknown handler
task exception propagation
memory over-budget detection
diagnostics disabled mode
window resize without RHI crash
D3D11 backend failure reports meaningful error
```

### Пункт 4. Smoke tests

Нужно иметь запускаемые smoke apps:

```text
HeadlessCoreApp
WindowSmokeApp
InputSmokeApp
RhiClearScreenApp
```

Smoke app не заменяет unit tests. Она нужна для проверки реального OS/GPU поведения.

### Пункт 5. Public API review

Нужно проверить public headers:

```text
Foundation/include
Memory/include
Diagnostics/include
Core/include
Platform/include
Input/include
RHI/include
RHI_D3D11/include
```

Нужно убрать из public API:

* лишние implementation details;
* D3D11 types из RHI;
* Win32 types из platform-agnostic interfaces, кроме специально выделенного `NativeWindowHandle`;
* gameplay terms;
* resource/script terms;
* временные имена;
* небезопасные raw owning pointers.

### Пункт 6. Dependency review

Нужно проверить:

* `Foundation` не зависит ни от чего;
* `Memory` не зависит от верхних слоев;
* `Diagnostics` не зависит от renderer/gameplay/resources;
* `Core` не зависит от Platform/Input/RHI;
* `Platform` не зависит от Input/RHI/D3D11;
* `Input` не зависит от Gameplay;
* `RHI` не зависит от D3D11;
* `RHI_D3D11` не зависит от Renderer/Gameplay/Resources;
* Apps могут зависеть от всех нужных нижних модулей.

### Пункт 7. API stability notes

Нужно создать документ:

```text
EngineBase/API_STABILITY.md
```

В нем нужно указать:

* какие контракты считаются стабильными;
* что можно менять свободно;
* что требует обновления tests;
* что требует architecture review;
* какие зависимости запрещены.

### Пункт 8. Final Definition of Done

`Stable Runtime Base` готов, если:

* существует изолированная структура `EngineBase`;
* есть отдельные библиотеки Foundation/Memory/Diagnostics/Core/Platform/Input/RHI/RHI_D3D11;
* есть memory tracking baseline;
* есть diagnostics/profiling baseline;
* есть frame loop/frame phases;
* есть worker-pool scheduler;
* есть Windows window;
* есть input snapshot;
* есть RHI abstraction;
* есть D3D11 clear-screen backend;
* есть smoke apps;
* есть unit/integration/regression tests;
* lower layers не знают о future gameplay/resource/script/renderer systems;
* дальнейшие слои можно подключать как отдельные библиотеки поверх этой базы.

## 6. Что явно не входит в этот этап

В этот этап не входят:

* VFS;
* Resource Manager;
* asset loading;
* texture loading;
* mesh loading;
* material system;
* renderer layer;
* frame graph renderer;
* scene graph;
* UI;
* audio;
* physics;
* gameplay framework;
* simulation layer;
* NPC;
* AI;
* scripting;
* save/load;
* world streaming;
* quests;
* dialogue;
* inventory;
* economy;
* ships;
* property ownership;
* weather gameplay;
* seasons gameplay;
* content pipeline.

Если во время разработки этого этапа возникает желание добавить одну из этих систем, ее нужно отложить.

## 7. Итоговая цель этапа

После завершения `Stable Runtime Base` проект должен иметь надежный нижний слой, который можно не держать постоянно в контексте при разработке gameplay, renderer и simulation.

Итоговый результат:

```text
Application starts
Core lifecycle works
Modules start in dependency order
Worker pool works
Memory tracking works
Diagnostics/profiling scopes work
Frame phases are explicit
Windows window opens
Input is collected
RHI is abstracted
D3D11 backend clears screen
Resize and close work
Tests pass
Smoke apps run
Upper systems are not present
Lower systems do not know about future upper systems
```

## 8. Итоговый чеклист выполнения

### Шаг 1. Структура `EngineBase`

* [x] Создана или приведена в порядок папка `EngineBase`
* [x] Создан модуль Foundation
* [x] Создан модуль Memory
* [x] Создан модуль Diagnostics
* [x] Создан модуль Core
* [x] Создан модуль Platform
* [x] Создан модуль Input
* [x] Создан модуль RHI
* [x] Создан модуль RHI_D3D11
* [x] Создана папка Apps
* [x] Создана папка Tests
* [x] Каждый модуль является отдельной CMake-библиотекой
* [x] Настроены public/private include boundaries (Проект Windows-only)
* [x] Проект собирается
* [x] CTest видит тесты

### Шаг 2. Foundation

* [x] Реализован `Result<T>`
* [x] Реализован `Error`
* [x] Реализован `StringId`
* [x] Реализован `NameId`
* [x] Реализован `ModuleId`
* [x] Реализован `ServiceId`
* [x] Реализован `EventTypeId`
* [x] Пустая строка дает invalid id
* [x] Реализован `Handle<T>`
* [x] Реализованы time primitives
* [x] Реализован базовый `Path`
* [x] Добавлены тесты Foundation

### Шаг 3. Memory / Allocators Baseline

* [x] Реализован memory tracking
* [x] Реализованы allocation tags
* [x] Реализован учет allocated bytes
* [x] Реализован учет peak allocated bytes
* [x] Реализован учет allocation count
* [x] Реализована per-tag statistics
* [x] Реализован memory budget interface
* [x] Реализован over-budget detection
* [x] Добавлены тесты Memory

### Шаг 4. Microkernel / Core

* [x] Реализован application lifecycle
* [x] Реализован module interface
* [x] Реализован dependency-aware module registry
* [x] Реализован reverse shutdown
* [x] Реализован service container
* [x] Реализован event bus
* [x] Реализован unsubscribe для event bus
* [x] Реализован task scheduler
* [x] Реализован worker pool
* [x] Реализована exception propagation из задач
* [x] Реализован configuration service
* [x] Core не зависит от Platform/Input/RHI/D3D11
* [x] Добавлены тесты Core

### Шаг 5. Diagnostics / Profiling Baseline

* [x] Реализован logger interface
* [x] Реализованы уровни логирования
* [x] В логах есть module/category/thread/timestamp
* [x] Реализован profiling scope API
* [x] Реализованы counters
* [x] Реализован startup/shutdown diagnostics output
* [x] Diagnostics можно отключить без падения
* [x] Добавлены тесты Diagnostics

### Шаг 6. Frame Loop / Frame Phases

* [ ] Реализован `FrameContext`
* [ ] Реализован `FrameIndex`
* [ ] Реализованы frame phases
* [ ] Зафиксирован порядок фаз кадра
* [ ] Зафиксированы main thread responsibilities
* [ ] Реализован stop request
* [ ] Реализован frame limit для smoke/tests
* [ ] Реализованы frame diagnostics counters/scopes
* [ ] Добавлены тесты Frame Loop

### Шаг 7. Platform / Windows / Window

* [ ] Реализован `IPlatformRuntime`
* [ ] Реализован `IWindow`
* [ ] Реализован `IWindowSystem`
* [ ] Реализован `NativeWindowHandle`
* [ ] Реализован Windows runtime
* [ ] Реализован process info
* [ ] Реализован high-resolution timer
* [ ] Реализована dynamic library loading
* [ ] Реализован symbol lookup
* [ ] Реализовано Win32 window creation
* [ ] Реализован message pump
* [ ] Реализованы close/resize/focus/minimize/restore events
* [ ] Реализованы DPI/client size basics
* [ ] Добавлены тесты Platform
* [ ] Создан `WindowSmokeApp`

### Шаг 8. Input

* [ ] Реализован `IInputSystem`
* [ ] Реализован `InputEvent`
* [ ] Реализован `InputSnapshot`
* [ ] Реализован `KeyboardState`
* [ ] Реализован `MouseState`
* [ ] Реализована обработка key down/up
* [ ] Реализована обработка mouse move
* [ ] Реализована обработка mouse buttons
* [ ] Реализована обработка mouse wheel
* [ ] Реализован сброс input state при потере фокуса
* [ ] Реализован per-frame input snapshot
* [ ] Input не содержит gameplay actions
* [ ] Добавлены тесты Input
* [ ] Создан `InputSmokeApp`

### Шаг 9. RHI

* [ ] Реализован `IRhiDevice`
* [ ] Реализован `IRhiSwapChain`
* [ ] Реализован `IRhiCommandContext`
* [ ] Реализованы RHI descriptors
* [ ] Реализованы `BeginFrame`
* [ ] Реализован `Clear`
* [ ] Реализован `EndFrame`
* [ ] Реализован `Present`
* [ ] Реализован `ResizeSwapChain`
* [ ] RHI не зависит от D3D11
* [ ] RHI не содержит renderer/gameplay/resource concepts
* [ ] Добавлены тесты RHI descriptors/contracts

### Шаг 10. RHI.D3D11

* [ ] Создан отдельный модуль `RHI_D3D11`
* [ ] Реализован D3D11 device creation
* [ ] Реализован debug layer flag
* [ ] Реализован feature level selection
* [ ] Реализована swap chain creation
* [ ] Реализован render target view
* [ ] Реализован clear screen через RHI
* [ ] Реализован present
* [ ] Реализован resize handling
* [ ] Minimized window не приводит к падению
* [ ] Ошибки D3D11/DXGI конвертируются в `Error`
* [ ] Создан `RhiClearScreenApp`

### Шаг 11. Интеграция

* [ ] Реализован общий application/frame loop
* [ ] Platform events проходят через frame loop
* [ ] Input обновляется в frame loop
* [ ] Event bus drain выполняется в frame loop
* [ ] Modules tick выполняется в frame loop
* [ ] RHI frame boundary выполняется в frame loop
* [ ] Scheduler корректно работает вместе с frame loop
* [ ] Diagnostics показывает порядок запуска
* [ ] Diagnostics показывает frame timing
* [ ] `HeadlessCoreApp` работает
* [ ] `WindowSmokeApp` работает
* [ ] `InputSmokeApp` работает
* [ ] `RhiClearScreenApp` работает

### Шаг 12. Тестирование и заморозка контрактов

* [ ] Написать отдельное покрытие каждого из модулей
* [ ] Unit tests проходят
* [ ] Integration tests проходят
* [ ] Regression tests проходят
* [ ] Smoke apps запускаются
* [ ] Public API review выполнен
* [ ] Dependency review выполнен
* [ ] Создан `EngineBase/API_STABILITY.md`
* [ ] В документации нет завышенных `[x]`
* [ ] Lower layers не зависят от future upper layers
* [ ] Документирование работы с ядром и план разработки дальнейших внешних слоев
* [ ] `Stable Runtime Base` готов для разработки следующих слоев
