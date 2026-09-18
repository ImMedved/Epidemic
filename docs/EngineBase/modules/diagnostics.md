# Diagnostics

## Назначение

Diagnostics делает работу EngineBase наблюдаемой через structured logging, числовые counters, scope profiling и диагностические имена потоков. Diagnostics не владеет authoritative engine/game state и не используется как event bus, control channel или условие успешности engine mutation.

## Logging

`ILogger` является безопасной публичной границей. Все `Log`/`Trace`/`Debug`/`Info`/`Warn`/`Error`/`Fatal` вызовы являются `noexcept`: исключение конкретного sink перехватывается внутри `ILogger` и не может заменить исходную ошибку либо изменить control flow движка.

Конкретный sink реализует private virtual `Write`. `ConsoleLogger` пишет в stdout и best-effort зеркалирует строку в `logs/epidemic.log`. Через `SetLoggingEnabled(false)` dispatch можно отключить полностью; пропущенные записи не replay-ятся после повторного включения.

`LogMessage` заимствует module/category/message только на время синхронного вызова. `thread_name` хранится как owning `std::string`.

## Counters

`DiagnosticsCounters` хранит фиксированный массив атомарных `int64_t`. `Increment` и `Decrement` используют saturating arithmetic и не wrap-around при достижении `INT64_MIN/MAX`. `CounterId::Count` и поврежденные enum values являются invalid: mutation для них является no-op, `Get` возвращает `0`.

Counters предназначены только для наблюдения. Они не являются revision, generation, ownership token или механизмом синхронизации.

## Profiling

`SetProfileCollector` задает process-wide collector для новых scopes. `SetProfilingEnabled(false)` отключает создание profiling events; disabled path возвращается до копирования имени scope и не делает диагностических allocations.

`ProfileScope` копирует имя scope и захватывает `shared_ptr` на текущий collector при создании. Замена global collector во время уже активного scope не перенаправляет его событие. Деструктор `ProfileScope` всегда `noexcept`, работает на normal return и exception unwinding и подавляет исключения collector, сохраняя исходную ошибку.

`InMemoryProfileCollector::Snapshot()` возвращает detached copy диагностических samples. Это не persistence snapshot и restore API у Diagnostics отсутствует.

Пример:

```cpp
epidemic::diagnostics::SetProfileCollector(collector);
epidemic::diagnostics::SetProfilingEnabled(true);
{
    epidemic::diagnostics::ProfileScope scope("Resources.ProcessPendingLoads");
    ProcessPendingLoads();
}
```

## Thread context

`SetCurrentThreadName` меняет только `thread_local` имя текущего потока. `GetCurrentThreadName` возвращает owning `std::string`, поэтому полученное значение не становится dangling после последующего rename или завершения исходного потока.

## Threading

Counters и enable flags атомарны. Global collector защищен mutex. `InMemoryProfileCollector` и `ConsoleLogger` сериализуют доступ к внутреннему состоянию. Thread names являются `thread_local`.

Полная cross-module concurrency qualification выполняется в Goal 6; локальный Diagnostics audit фиксирует только собственные synchronization contracts.

## Карта публичных заголовков

- `console_logger.h`: `ConsoleLogger`.
- `counters.h`: `CounterId`, `DiagnosticsCounters`, counter helpers.
- `logger.h`: `LogLevel`, `LogMessage`, `ILogger`, logging enable state.
- `profiling.h`: `ProfileEvent`, `IProfileCollector`, `InMemoryProfileCollector`, `ProfileScope`.
- `thread_context.h`: current-thread diagnostic name helpers.
