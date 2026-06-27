# Модули и состав проекта

## Текущие модули `core`

### `core/application`

Отвечает за lifecycle приложения:

- bootstrap;
- initialize;
- run;
- shutdown;
- регистрацию базовых сервисов;
- orchestration module registry.

### `core/modules`

Отвечает за:

- `IModule`;
- manifest модуля;
- зависимости модулей;
- вычисление execution plan;
- обратный порядок shutdown.

### `core/services`

Содержит typed service container. Это единственное место, где в новом коде можно регистрировать и получать core/runtime services.

### `core/events`

Содержит typed event bus:

- sync dispatch;
- queued dispatch;
- drain queued events.

### `core/tasks`

Содержит базовый scheduler:

- worker thread pool stub;
- queue задач;
- ожидание `idle`.

### `core/diagnostics`

Содержит logger contracts и базовую console implementation.

### `core/config`

Содержит config contracts и текущую in-memory implementation.

## Текущий `foundation`

### `foundation/error`

Содержит базовый тип `Error` для описания отказов без привязки к конкретной подсистеме.

### `foundation/result`

Содержит `Result<T>` и `Result<void>` как общий контракт успешного результата или ошибки.

### `foundation/paths`

Содержит тип `Path`, который уже используется в runtime-контрактах вместо прямого протаскивания `std::filesystem::path` между слоями.

### `foundation/ids`

Содержит `StringId` и `NameId` как легкие strongly-typed идентификаторы поверх стабильного hash-представления строки.

### `foundation/handles`

Содержит `Handle<T>` как typed handle с `index + generation`, пригодный для ресурсов, scene-объектов, сущностей и других runtime-таблиц.

## Текущий слой `platform`

### `layers/platform/interfaces`

Содержит platform-agnostic contracts. На текущем этапе там находятся `IPlatformRuntime`, `ProcessInfo` и контракты для dynamic library loading.

### `layers/platform/windows`

Содержит Windows-first runtime implementation. Сейчас слой уже умеет:

- отдавать `ProcessInfo`;
- предоставлять monotonic clock;
- загружать dynamic libraries;
- искать exported symbols;
- участвовать в composition root как typed platform service.

## Текущий слой `runtime`

### `layers/runtime/interfaces`

Содержит только контракты:

- `IVirtualFileSystem`
- `IResourceManager`
- `IRenderer`
- `IScriptHost`

### `layers/runtime/placeholders`

Содержит временные stub/null implementations этих контрактов. Они нужны, чтобы `core` уже умел собирать runtime composition, не дожидаясь реальных подсистем.

## Текущий app host

### `apps/epidemic_app`

Содержит:

- entry point;
- composition root;
- demo modules, которые демонстрируют lifecycle, события и задачи.

Это технический bootstrap host, а не игровое приложение.

## Правила развития модулей

- новый код сначала получает собственную папку;
- интерфейс и реализация живут рядом внутри своего модуля;
- зависимости между модулями должны быть явными;
- `core` не разрастается в сторону rendering, resources или gameplay;
- если подсистема не является частью orchestration, ей не место в `core`.

## Что будет следующим

Следующие крупные модули должны появляться в `layers/*`:

- `layers/rhi`
- `layers/renderer`
- `layers/runtime/vfs`
- `layers/runtime/resources`
- `layers/gameplay`
- `layers/tools`
