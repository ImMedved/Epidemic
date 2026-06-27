# Слои и границы

## Общий принцип

Проект делится на пять основных зон:

- `src/foundation` — базовые value-type primitives;
- `src/core` — микроядро и только микроядро;
- `src/layers` — подключаемые слои runtime;
- `src/apps` — executable hosts и composition roots;
- `tests` — unit, integration и regression tests.

Архитектурные границы должны быть видны прямо по структуре каталогов.

## Правило `core`

`core` не знает:

- о DX11;
- о форматах ресурсов;
- о gameplay logic;
- о platform-specific деталях beyond contracts.

`core` знает только про:

- lifecycle;
- typed services;
- modules;
- events;
- tasks;
- diagnostics;
- configuration.

## Правило `foundation`

`foundation` находится ниже `core` и всех слоев. Это место только для маленьких и стабильных примитивов:

- `Result` / `Error`;
- `Path`;
- `StringId` / `NameId`;
- `Handle<T>`.

Если тип не является общим низкоуровневым примитивом, ему не место в `foundation`.

## Правило `layers`

Каждый слой живет в своей подпапке и не растекается по проекту.

Текущая форма:

```text
src/foundation/
  error/
  result/
  paths/
  ids/
  handles/

src/layers/
  platform/
    interfaces/
    windows/
  runtime/
    interfaces/
    placeholders/
```

Следующими должны появляться:

- `rhi/`
- `renderer/`
- `gameplay/`
- `tools/`

## Правило границ между слоями

- слой зависит от `core`, но не наоборот;
- `core` и `layers` могут зависеть от `foundation`;
- public contracts слоя лежат внутри самого слоя;
- placeholder и stub-реализации лежат рядом со своим слоем;
- executable host в `src/apps/*` собирает composition root, но не переносит логику обратно в ядро.

## Почему это важно

Без жестких границ проект быстро деградирует в:

- размытые зависимости;
- скрытые сервисные связи;
- глобальное состояние;
- смешение runtime, renderer и gameplay.

Эта структура нужна для архитектурной дисциплины, а не ради косметики.

## Текущий статус

- `core` уже физически отделен от слоев;
- `foundation` уже существует как отдельная базовая зона;
- `runtime` уже существует как отдельный слой с placeholder interfaces;
- `platform` уже существует как отдельный слой с Windows-first runtime implementation;
- дальнейшие subsystem-реализации должны добавляться только внутрь `layers/*`.
