# Микроядро Epidemic Engine

## Назначение

Микроядро в `Epidemic Engine v1.0` отвечает только за orchestration runtime. Его задача — управлять жизненным циклом приложения, сервисами, модулями, событиями и задачами, не втягивая в себя renderer, gameplay и контентные подсистемы.

## Что входит в микроядро

- жизненный цикл приложения: `bootstrap -> initialize -> run -> shutdown`;
- реестр модулей и вычисление dependency-aware порядка выполнения;
- typed service container;
- bootstrap diagnostics и configuration;
- sync/queued event bus;
- task scheduler;
- безопасное завершение модулей в обратном порядке;
- защита от повторной некорректной инициализации после lifecycle failure.

## Что не входит в микроядро

- DirectX 11 и любой конкретный rendering backend;
- реальная реализация VFS и Resource Manager;
- platform runtime implementations;
- scripting VM;
- gameplay systems;
- composition root приложения.

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

Каждая подпапка отвечает только за один технический аспект базового runtime.

## Жизненный цикл

### Bootstrap

- регистрируются только core typed services;
- вычисляется порядок модулей;
- модули проходят стадию `OnBootstrap`.

### Initialize

- модули инициализируются в уже рассчитанном порядке;
- здесь допустимо подписываться на события;
- runtime-level services уже доступны, если их зарегистрировал host.

### Run

- дренируются queued events;
- дожидаются завершения запланированных задач;
- это один runtime slice, а не полноценный platform loop.

### Shutdown

- модули завершаются строго в обратном порядке;
- task scheduler доводится до idle state;
- runtime завершает работу без raw owning state.

## Модель модулей

Каждый модуль предоставляет:

- manifest с `id`, `name` и зависимостями;
- `OnBootstrap`;
- `OnInitialize`;
- `OnShutdown`.

Порядок выполнения всегда вычисляет само микроядро.

## Модель сервисов

Service container поддерживает только typed interfaces:

- `RegisterInstance<T>()`
- `Emplace<TService, TImplementation>()`
- `Get<T>()`
- `Contains<T>()`

Контракты:

- null service instance запрещен;
- повторная регистрация одного typed service запрещена;
- новый код не использует строковой service locator;
- `core` регистрирует только свои default-сервисы.

## События

Микроядро поддерживает две модели событий:

- `PublishSync(event)` — немедленная доставка;
- `Enqueue(event)` + `DrainQueued()` — отложенная доставка;
- `Unsubscribe(token)` — явная отписка handler'ов.

Для queued dispatch зафиксирован базовый FIFO-контракт, который закреплен тестами.

## Task Scheduler

На первом этапе используется простой scheduler на `std::jthread`:

- принимает задачи через `Schedule`;
- умеет ждать `idle` через `WaitIdle`;
- не использует fibers;
- не проглатывает исключения бесследно, а пробрасывает первую ошибку через `WaitIdle`.

## Гарантии текущей реализации

- проект собирается через CMake;
- исполняемый файл стартует;
- модули проходят bootstrap, initialize и shutdown;
- shutdown идет в обратном порядке, включая частично bootstrapped состояние после ошибки;
- typed services реально используются;
- sync/queued events работают;
- scheduler исполняет простые задачи и не скрывает падения;
- есть базовые и регрессионные тесты на container, lifecycle, events, scheduler и composition root.

## Что считать завершенным в блоке Microkernel

Блок `1. Microkernel` сейчас можно считать закрытым на длительный этап работ, потому что:

- composition root уже вынесен в `src/apps/*`;
- ядро не зависит от DX11;
- ядро не зависит от concrete platform/runtime implementations;
- новые слои можно подключать поверх `core`, не меняя модель приложения;
- дальнейшее развитие должно происходить в `layers/*`, а не внутри базовой orchestration-модели.
