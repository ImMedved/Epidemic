# Микроядро Epidemic Engine

## Назначение

Микроядро в `Epidemic Engine v1.0` отвечает только за orchestration runtime, но не за игровую логику, графику, совместимость со Storm или форматы ресурсов. Его задача — быть маленьким, предсказуемым и долгоживущим центром композиции.

## Что входит в микроядро

- жизненный цикл приложения: `bootstrap -> initialize -> run -> shutdown`;
- реестр модулей и вычисление порядка выполнения;
- typed service container;
- bootstrap диагностических сервисов;
- bootstrap конфигурации;
- bootstrap event bus;
- bootstrap task scheduler;
- безопасное завершение модулей в обратном порядке.

## Что не входит в микроядро

- DirectX 11 и любой конкретный rendering backend;
- Storm compatibility layer;
- VFS и Resource Manager как реальные подсистемы;
- scripting VM;
- gameplay logic;
- platform-specific windowing beyond future contracts.

## Текущая структура

```text
src/core/
  application/
  config/
  diagnostics/
  events/
  modules/
  services/
  tasks/
```

Каждая подпапка отвечает только за один технический аспект микроядра.

## Жизненный цикл

### Bootstrap

- регистрируются базовые typed services;
- вычисляется dependency-aware порядок модулей;
- модули проходят стадию `OnBootstrap`.

### Initialize

- модули инициализируются в уже рассчитанном порядке;
- на этом этапе допустимо подписываться на события и публиковать sync events;
- runtime-level services уже доступны по typed interface.

### Run

- дренируются queued events;
- дожидаются завершения запланированных задач;
- хост-приложение получает стабильную headless execution loop baseline.

### Shutdown

- модули завершаются строго в обратном порядке относительно execution plan;
- task scheduler доводится до idle state;
- приложение завершает работу без утечек raw owning state.

## Модель модулей

Модуль обязан предоставить:

- manifest с `id`, `name` и списком зависимостей;
- `OnBootstrap`;
- `OnInitialize`;
- `OnShutdown`.

Микроядро само вычисляет порядок выполнения по зависимостям. Это защищает нас от хрупкого "ручного порядка регистрации" в `main`.

## Модель сервисов

Service container поддерживает только typed interfaces:

- `RegisterInstance<T>()`
- `Emplace<TService, TImplementation>()`
- `Get<T>()`
- `Contains<T>()`

Особенности:

- null service instance запрещен;
- повторная регистрация одного typed service запрещена;
- старые string-based aliases здесь не допускаются;
- старые service names будут жить только в compatibility layer.

## События

Сейчас микроядро поддерживает две модели событий:

- `PublishSync(event)` — немедленная доставка;
- `Enqueue(event)` + `DrainQueued()` — отложенная доставка.

Это уже дает правильную границу между логикой рантайма и фоновой работой. Legacy string events будут добавлены позже поверх адаптеров, но не внутри `core`.

## Task Scheduler

На первом этапе используется простой scheduler на `std::jthread`:

- принимает задачи через `Schedule`;
- умеет ждать состояния `idle` через `WaitIdle`;
- не использует fibers;
- уже покрыт тестом как базовый execution primitive.

Этого достаточно для раннего bootstrap и дальнейшего перехода к VFS/Resource pipeline.

## Гарантии текущей реализации

- проект собирается через CMake;
- исполняемый файл стартует;
- модули проходят bootstrap/init/shutdown;
- shutdown идет в обратном порядке;
- typed services реально используются, а не имитируются;
- sync/queued events работают;
- scheduler исполняет простые задачи;
- есть базовые тесты на container, lifecycle, events и scheduler.

## Что считать завершенным в блоке Microkernel

На текущем этапе блок `1. Microkernel` считается закрытым для долгого цикла работ, потому что:

- composition root уже существует;
- ядро не зависит от Storm-кода;
- ядро не зависит от DX11;
- слои можно подключать поверх `core`, не меняя саму модель приложения;
- дальнейшие изменения будут происходить в `layers/*`, а не внутри базовой orchestration-модели.
