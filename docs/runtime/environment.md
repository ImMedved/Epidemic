# Environment

## Purpose

Represents weather, season, climate and surface state as runtime data and projections.

## Includes

- weather, season, climate and surface value types
- `IEnvironmentRuntime`
- `EnvironmentSnapshot`, `EnvironmentProjection`
- external `EnvironmentUpdateInput`
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

## Forbidden Dependencies

No dependency on `Time`, `World`, `Scene`, `Renderer` or `Physics`.

## States

- weather kind: `Clear` to `Transitioning`
- season kind: `Spring` to `Transitioning`
- surface condition: `Dry` to `Drying`

## How To Use

Set region weather/season/climate and surface state directly, then build snapshot/projection copies for downstream consumers.

## Example

```cpp
environment.SetWeather(region, weather);
auto projection = environment.BuildProjection(region);
environment.Update({game_time_ticks, game_delta_ticks, region});
```

## Testing Strategy

Validate default weather, set/get behavior, projection copy semantics and deterministic drying behavior.
