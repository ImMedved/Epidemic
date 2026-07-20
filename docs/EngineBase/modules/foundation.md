# Foundation

## Назначение

Foundation — самый нижний модуль EngineBase. Он предоставляет маленькие типы, на которых строятся остальные библиотеки, и не зависит от других частей движка. Сюда попадает только то, что одинаково необходимо Core, Platform, Memory, RHI и будущему Runtime.

## Модель

Ошибки передаются через `Error` и `Result<T>`. Идентификаторы и handles являются строгими типами, чтобы разные пространства ID нельзя было случайно смешать. `Path` нормализует работу с путями без превращения Foundation в файловую систему. `FrameTime` и `FrameIndex` описывают базовые значения кадра.

| Контракт | Роль |
|---|---|
| `Error` | стабильный код, сообщение и контекст runtime failure |
| `Result<T>` | значение либо `Error` без скрытого global error state |
| `BasicId`, `StringId` | типобезопасные идентификаторы |
| `Handle` | пара ID/generation для проверки stale references |
| `Path` | легкий value type пути |
| `FrameTime`, `FrameIndex` | базовые значения frame loop |

`Result<T>` используется для ожидаемых ошибок внешней среды и проверки данных. Он не заменяет assertions для нарушений внутреннего контракта.

## Инварианты

Invalid ID и handle имеют явно определенное нулевое состояние. Сравнение и hashing должны быть детерминированными. Foundation не выполняет I/O, не создает threads и не владеет engine services.

## Использование

Верхние модули объявляют собственные tag-типы и строят `BasicId<Tag>` или `Handle<Id>`, а не передают необозначенные целые числа. Ошибки получают стабильные machine-readable codes, чтобы tests и diagnostics не зависели от текста сообщения.

## Ограничения и стабильность

Foundation не является общей свалкой helpers. Контейнеры, math subsystem, filesystem, serialization и gameplay utilities должны жить в своих слоях. Публичные value types заморожены; расширения допустимы только без изменения существующей семантики.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `error.h`: `Error`.
- `handle.h`: `Handle`.
- `path.h`: `Path`.
- `result.h`: `Result`.
- `string_id.h`: `BasicId`, `StringIdTag`, `NameIdTag`, `ModuleIdTag`, `ServiceIdTag`, `EventTypeIdTag`.
- `time.h`: `FrameTime`, `FrameIndex`.
