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

## Текущий слой `runtime`

### `layers/runtime/interfaces`

Содержит только контракты:

- `IVirtualFileSystem`
- `IResourceManager`
- `IRenderer`
- `IScriptHost`

### `layers/runtime/placeholders`

Содержит временные stub/null implementations этих контрактов. Они нужны, чтобы `core` уже умел собирать runtime composition, не дожидаясь реальных подсистем.

## Текущий слой `platform`

### `layers/platform/interfaces`

Содержит platform-agnostic contracts для хост-платформы. На текущем этапе это минимальный runtime interface, через который `core` может обращаться к платформенному слою без знания о Win32 API.

### `layers/platform/placeholders`

Содержит Windows-first stub implementation. Это еще не полноценный platform subsystem, а точка расширения под следующий этап, где появятся process startup, Win32 windowing и platform event pump.

## Текущий app host

### `apps/epidemic_app`

Содержит:

- entry point;
- composition root;
- demo modules, которые демонстрируют lifecycle, события и задачи.

Это не игровое приложение и не финальный runtime shell. Это технический bootstrap host.

## Правила развития модулей

- новый код сначала получает собственную папку;
- интерфейс и реализация живут рядом внутри своего модуля;
- зависимости между модулями должны быть явными;
- `core` не разрастается в сторону rendering, resources или compatibility;
- если подсистема не является частью orchestration, ей не место в `core`.

## Что будет следующим

После завершения блока `Microkernel` новые крупные модули должны появляться уже в `layers/*`:

- `layers/platform`
- `layers/rhi`
- `layers/renderer`
- `layers/runtime/vfs`
- `layers/runtime/resources`
- `layers/compatibility`
- `layers/gameplay_host`
