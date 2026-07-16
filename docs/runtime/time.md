# Time

## Purpose

`Time` is the authoritative runtime major for deterministic game time. It owns real-time to game-second conversion, pause/resume state, positive time scale, controlled skips, calendar conversion, day phase calculation, immutable snapshots and time transition events.

## Public Contracts

- `game_time.h`: uses shared `GameTimePoint` and `GameDuration` from `RuntimeFoundation`; `GameTime` is a compatibility alias.
- `game_calendar.h`: configurable `CalendarDefinition` and generic `CalendarDate`.
- `time_state.h`: generic `DayPhase` and internal runtime state labels.
- `time_snapshot.h`: immutable `TimeSnapshot` with `revision`.
- `time_events.h`: `TimeAdvanced`, `TimeScaleChanged`, `Paused`, `Resumed`, `TimeJumped`, `DayChanged`, `DayPhaseChanged`.
- `time_runtime.h`: read-only `IGameClock`, mutable `ITimeRuntime`, `TimeOptions`, `TimeAdvanceResult` and `CreateTimeServices`.

## Rules

- Public time values are typed, not raw integers.
- One `GameTimePoint` tick is one game second.
- `TimeOptions::game_ticks_per_real_second` is the base game-seconds-per-real-second rate.
- `time_scale` multiplies that base rate; `time_scale = 2.0` means twice as many game seconds advance for the same real delta.
- `Advance(std::chrono::microseconds)` uses an accumulator so fractional game seconds carry into later frames.
- `SetTimeScale()` rejects zero, negative, NaN and infinity with `time.invalid_scale`.
- `CreateTimeServices()` validates `TimeOptions` before creating the runtime.
- Pause is stored separately from time scale.
- `Skip()` changes authoritative time and emits `TimeJumped`; it does not call Environment, Simulation, Persistence or other majors.
- Calendar conversion uses the same unit as the clock: one tick is one game second; calendar hours and minutes are derived from seconds.
- Snapshot `revision` increments only when observable time state changes.
- Day phase boundaries are configuration-driven, must be inside the configured day and must not share the same start minute.

## Forbidden Dependencies

`Time` must not depend on weather, gameplay schedules, quest timers, NPC logic, `Environment`, `Persistence`, `World` or `Simulation` implementations.

## Example

```cpp
auto services = CreateTimeServices({}).Value();
services.runtime->SetTimeScale(2.0);
services.runtime->Advance(std::chrono::milliseconds(16));

const TimeSnapshot snapshot = services.clock->GetSnapshot();
```

## Testing Strategy

The `Time` tests cover deterministic accumulation, equivalent frame splits, fractional remainder, pause/resume, invalid options, invalid scale, skip events, calendar conversion, date validation, day/phase transitions, revision stability and service factory registration shape.
