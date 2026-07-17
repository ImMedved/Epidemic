# Environment

## Purpose

Represents weather, season, climate and surface state as runtime data and projections.

## Includes

- weather, season, climate and surface value types
- `IEnvironmentRuntime`
- `EnvironmentSnapshot`, `EnvironmentProjection`
- external `EnvironmentUpdateInput`
- atomic `EnvironmentStateUpdate`
- deterministic in-memory runtime implementation

## Excludes

- time ownership
- renderer materials
- physics friction backend
- gameplay AI rules

## Public Contracts

- `weather_state.h`
- `season_state.h`
- `climate_profile.h`
- `surface_state.h`
- `environment_snapshot.h`
- `environment_projection.h`
- `environment_update.h`
- `environment_runtime.h`
- `environment_services.h`

## Forbidden Dependencies

No dependency on `Time`, `World`, `Scene`, `Renderer` or `Physics`.

## States

- weather kind: `Clear` to `Transitioning`
- season kind: `Spring` to `Transitioning`
- surface wetness, snow, mud and ice values are authoritative
- surface condition is derived from numeric surface state

## How To Use

Bootstrap region weather/season/climate and surface state through the writer, then build snapshot/projection copies for downstream consumers.

Runtime policy ownership is `std::shared_ptr<const IEnvironmentUpdatePolicy>`. Policies build `EnvironmentStateUpdate` batches from read-only query state; they do not mutate the writer directly.

`ApplyUpdate()` validates `source_revision`, region ownership, weather/season/climate/surface ranges and all surfaces before mutating anything. A successful batch increments revision once and stamps changed surfaces with that revision. Failed batches leave state unchanged.

`EnvironmentUpdateInput` uses `GameTimePoint` and `GameDuration`.

## Example

```cpp
environment.SetWeather(region, weather);
auto projection = environment.BuildProjection(region);
environment.Update({game_time, game_delta, region});
```

## Testing Strategy

Validate unknown ids, set/get behavior, range validation, atomic batch conflicts, invalid-batch rollback, policy-built updates, typed time input, projection copy semantics, derived surface condition and wind direction normalization.
