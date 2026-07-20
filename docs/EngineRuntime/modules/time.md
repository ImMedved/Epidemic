# Time

## Назначение

Time превращает реальную длительность кадра в authoritative игровое время, календарь, фазу дня и события переходов. Это единственный источник game delta для Runtime.

## Контракты

`IGameClock` выдает текущую точку, последний delta и snapshot. `ITimeRuntime` выполняет `Advance`, Pause, Resume, изменение `TimeScale` и Skip. `TimeAdvanceResult` содержит previous/current snapshot и список `TimeEvent`.

`CalendarDefinition` задает длину дня, месяца и года. `PhaseBoundary` связывает минуту дня с `DayPhase`.

## Поведение

Дробные game ticks накапливаются integer/rational remainder без floating drift. Изменение scale и ручной Skip сбрасывают remainder по зафиксированному контракту. `last_delta` является transient полем и не меняет authoritative revision сам по себе.

## Стабильность

Модуль frozen. Environment, Simulation и gameplay получают время через clock/result, а не рассчитывают собственную альтернативную шкалу.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `game_calendar.h`: `CalendarDefinition`, `CalendarDate`.
- `game_time.h`: factory-функции или backend implementation без отдельного публичного типа.
- `time_events.h`: `TimeEventKind`, `TimeEvent`.
- `time_runtime.h`: `TimeAdvanceResult`, `PhaseBoundary`, `TimeOptions`, `IGameClock`, `ITimeRuntime`, `TimeServices`.
- `time_scale.h`: `TimeScale`.
- `time_snapshot.h`: `TimeSnapshot`.
- `time_state.h`: `DayPhase`, `TimeRuntimeState`.
