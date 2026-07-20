# Diagnostics

## Назначение

Diagnostics делает состояние низкого слоя наблюдаемым: пишет structured log messages, считает события, измеряет scopes и присваивает понятные имена потокам. Работа движка не должна зависеть от конкретного output sink.

## Модель

`ILogger` принимает `LogMessage` с уровнем, module и текстом. `ConsoleLogger` является базовой реализацией для консоли и файла. `DiagnosticsCounters` хранит именованные counters. `IProfileCollector` принимает `ProfileEvent`, а `ProfileScope` формирует RAII-измерение. Thread context helpers устанавливают диагностическое имя текущего потока.

Diagnostics не определяет gameplay telemetry и не хранит бизнес-аналитику игры. Его события относятся к работе engine/runtime.

## Использование

Код получает `ILogger` через service container и логирует на границах операции: initialization, recoverable failure, shutdown и изменение важного состояния. Горячие внутренние циклы не должны создавать большие строки каждый кадр без необходимости.

Profiling scope используется вокруг значимой операции:

```cpp
epidemic::diagnostics::ProfileScope scope(
    collector,
    "Resources.ProcessPendingLoads");
```

Counters подходят для количества queued events, processed jobs, failed loads и других агрегатов, которые не требуют отдельной записи на каждое событие.

## Ограничения и стабильность

Отключение profiling collector или сокращение logger output не должно менять поведение программы. Diagnostics не владеет authoritative state и не используется как канал связи между modules. Публичные interfaces стабильны; новые sinks и collectors можно добавлять без их изменения.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `console_logger.h`: `ConsoleLogger`.
- `counters.h`: `CounterId`, `DiagnosticsCounters`.
- `logger.h`: `LogLevel`, `LogMessage`, `ILogger`.
- `profiling.h`: `ProfileEvent`, `IProfileCollector`, `InMemoryProfileCollector`, `ProfileScope`.
- `thread_context.h`: factory-функции или backend implementation без отдельного публичного типа.
