# Time

## Purpose

Provides authoritative game time, calendar conversion, pause/resume, time scale, skip and time snapshots/events.

## Includes

- `GameTime`, `GameDuration`, `CalendarDate`
- `ITimeRuntime` and read-only `IGameClock`
- `TimeSnapshot`, `TimeEventKind`, `TimeEvent`
- in-memory `TimeRuntime`

## Excludes

- weather ownership
- NPC schedules
- quest timers
- streaming policy

## Public Contracts

- `game_time.h`
- `game_calendar.h`
- `time_state.h`
- `time_runtime.h`
- `time_snapshot.h`
- `time_events.h`

## Forbidden Dependencies

No dependency on `Environment`, `Persistence`, `World` or `Simulation`.

## States

`TimeRuntimeState` includes running, paused, scale changed, skipping, jumped, day changed and phase changed.

## How To Use

Advance through `Update(real_delta)`, read snapshots for consumers and use pause/scale/skip controls on the runtime.

## Example

```cpp
runtime.SetTimeScale(2.0f);
runtime.Update({16});
auto snapshot = runtime.GetSnapshot();
```

## Testing Strategy

Validate advance, pause/resume, scale, skip, calendar conversion and day-phase thresholds.
