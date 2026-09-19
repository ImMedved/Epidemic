# Time

## Назначение

Time превращает реальную длительность кадра в authoritative игровое время, календарь, фазу дня и события переходов. Это единственный источник game delta для Runtime.

## Контракты

`IGameClock` выдает текущую точку, последний delta и frame-facing `TimeSnapshot`. `TimeSnapshot` является наблюдением текущего кадра и не является persistence-format.

`ITimeRuntime` выполняет `Advance`, Pause, Resume, изменение `TimeScale` и Skip. Для persistence используется `TimeCheckpoint`: он сохраняет authoritative clock state, нормализованный `TimeScale`, paused state, fractional remainder, revision и immutable configuration identity. `RestoreCheckpoint` сначала полностью проверяет candidate и совместимость конфигурации, затем выполняет один no-fail commit. Ошибка validation или allocation до commit не изменяет live clock. `last_delta`, текущий event buffer и operation-only state в checkpoint не входят.

`TimeAdvanceResult` содержит previous/current snapshot и список `TimeEvent`. `CalendarDefinition` задает длину дня, месяца и года. `PhaseBoundary` связывает минуту дня с `DayPhase`; effective phase boundaries хранятся в canonical sorted form, а пустой список использует стандартные boundaries.

## Поведение

Дробные game ticks накапливаются integer/rational remainder без floating drift. Изменение remainder является authoritative mutation и увеличивает revision даже если целый game tick ещё не получен. Изменение scale и ненулевой ручной Skip сбрасывают remainder по зафиксированному контракту. `last_delta` является transient полем и сам по себе authoritative revision не увеличивает.

Pause/Resume и повторные no-op операции сохраняют deterministic semantics. Большой `Advance` или Skip публикует обязательные `DayChanged` и `DayPhaseChanged`, если соответствующие границы были пересечены, даже когда конечная фаза совпадает с начальной. Event order для одной операции стабилен: основной operation event, затем `DayChanged`, затем `DayPhaseChanged`.

## Стабильность

Модуль прошёл локальный Goal 3 Time audit и является `LOCAL_READY` кандидатом для системных Goals 5–9. Статус `FROZEN` здесь не утверждается до whole-engine qualification. Environment, Simulation и gameplay получают время через clock/result, а не рассчитывают собственную альтернативную шкалу.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `game_calendar.h`: `CalendarDefinition`, `CalendarDate`.
- `game_time.h`: alias/helpers для game time.
- `time_events.h`: `TimeEventKind`, `TimeEvent`.
- `time_runtime.h`: `TimeAdvanceResult`, `PhaseBoundary`, `TimeOptions`, `TimeCheckpoint`, `IGameClock`, `ITimeRuntime`, `TimeServices`.
- `time_scale.h`: `TimeScale`.
- `time_snapshot.h`: `TimeSnapshot`.
- `time_state.h`: `DayPhase`, `TimeRuntimeState`.
