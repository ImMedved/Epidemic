# Time

## Purpose

`Time` is the authoritative runtime major for deterministic game time. It owns real-time to game-second conversion, pause/resume state, positive rational time scale, controlled skips, calendar conversion, day phase calculation, immutable snapshots and time transition events.

## Public Contracts

- `game_time.h`: uses shared `GameTimePoint` and `GameDuration` from `RuntimeFoundation`; `GameTime` is a compatibility alias.
- `game_calendar.h`: configurable `CalendarDefinition` and generic `CalendarDate` with second precision.
- `time_scale.h`: fixed rational `TimeScale { numerator, denominator }`.
- `time_state.h`: generic `DayPhase` and internal runtime state labels.
- `time_snapshot.h`: immutable `TimeSnapshot` with `revision`.
- `time_events.h`: `TimeAdvanced`, `TimeScaleChanged`, `Paused`, `Resumed`, `TimeJumped`, `DayChanged`, `DayPhaseChanged`.
- `time_runtime.h`: read-only `IGameClock`, mutable `ITimeRuntime`, `TimeOptions`, `TimeAdvanceResult` and `CreateTimeServices`.

## Rules

- Public time values are typed, not raw integers.
- One `GameTimePoint` tick is one game second.
- `TimeOptions::game_ticks_per_real_second` is the base game-seconds-per-real-second rate.
- `time_scale` multiplies that base rate as a rational value; `TimeScale{2, 1}` means twice as many game seconds advance for the same real delta.
- `Advance(std::chrono::microseconds)` uses an accumulator so fractional game seconds carry into later frames.
- `SetTimeScale()` rejects non-positive numerators and denominators with `time.invalid_scale`; accepted scales are normalized before they enter snapshots.
- `CreateTimeServices()` validates `TimeOptions` before creating the runtime.
- Pause is stored separately from time scale.
- `Skip()` changes authoritative time and emits `TimeJumped`; it does not call Environment, Simulation, Persistence or other majors.
- `Skip()` rejects negative durations with `time.invalid_skip`; reverse time is not supported by this runtime.
- `Advance()`, `Skip()` and calendar-to-time conversion reject arithmetic overflow with `time.overflow`.
- Calendar conversion uses the same unit as the clock: one tick is one game second; calendar hours, minutes and seconds are preserved during round trips.
- Snapshot `revision` increments only when observable time state changes.
- Day phase boundaries are configuration-driven, must be inside the configured day and must not share the same start minute.

## Forbidden Dependencies

`Time` must not depend on weather, gameplay schedules, quest timers, NPC logic, `Environment`, `Persistence`, `World` or `Simulation` implementations.

## Example

```cpp
auto services = CreateTimeServices({}).Value();
services.runtime->SetTimeScale(TimeScale{2, 1});
services.runtime->Advance(std::chrono::milliseconds(16));

const TimeSnapshot snapshot = services.clock->GetSnapshot();
```

## Testing Strategy

The `Time` tests cover deterministic accumulation, equivalent frame splits, fractional remainder, long runs, pause/resume, invalid and normalized scales, skip events, negative skip rejection, seconds round trip, overflow rejection, calendar conversion, date validation, day/phase transitions, revision stability and service factory registration shape.
