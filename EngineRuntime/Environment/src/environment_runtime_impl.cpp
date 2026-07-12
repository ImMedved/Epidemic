#include "environment_runtime_impl.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime
{
// Function note: Gets weather.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
WeatherState EnvironmentRuntime::GetWeather(RegionId region) const
{
    const auto iterator = weather_by_region_.find(region);
    if (iterator == weather_by_region_.end())
    {
        return WeatherState{};
    }

    return iterator->second;
}

// Function note: Gets season.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
SeasonState EnvironmentRuntime::GetSeason(RegionId region) const
{
    const auto iterator = season_by_region_.find(region);
    if (iterator == season_by_region_.end())
    {
        return SeasonState{};
    }

    return iterator->second;
}

// Function note: Gets surface state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<SurfaceState> EnvironmentRuntime::GetSurfaceState(SurfaceId surface) const
{
    const auto iterator = surface_states_.find(surface);
    if (iterator == surface_states_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

// Function note: Builds snapshot.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
EnvironmentSnapshot EnvironmentRuntime::BuildSnapshot(RegionId region) const
{
    // Function note: Gets climate profile.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ClimateProfile climate = GetClimateProfile(region);
    return EnvironmentSnapshot{
        region,
        GetWeather(region),
        GetSeason(region),
        climate,
        climate.average_temperature,
        climate.average_humidity,
    };
}

// Function note: Builds projection.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
EnvironmentProjection EnvironmentRuntime::BuildProjection(RegionId region) const
{
    // Function note: Builds snapshot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const EnvironmentSnapshot snapshot = BuildSnapshot(region);
    return EnvironmentProjection{snapshot.region_id, snapshot.weather, snapshot.season, snapshot.temperature, snapshot.humidity};
}

// Function note: Updates the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> EnvironmentRuntime::Update(const EnvironmentUpdateInput& input)
{
    if (!input.region_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("environment.invalid_region", "environment update requires a valid region id"));
    }

    if (input.game_delta_ticks <= 0)
    {
        return foundation::Result<void>::Success();
    }

    // Function note: Handles apply drying.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ApplyDrying(input.game_delta_ticks);
    return foundation::Result<void>::Success();
}

// Function note: Sets weather.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void EnvironmentRuntime::SetWeather(RegionId region, WeatherState weather)
{
    weather_by_region_[region] = weather;
}

// Function note: Sets season.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void EnvironmentRuntime::SetSeason(RegionId region, SeasonState season)
{
    season_by_region_[region] = season;
}

// Function note: Sets climate profile.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void EnvironmentRuntime::SetClimateProfile(RegionId region, ClimateProfile climate)
{
    climate_by_region_[region] = climate;
}

// Function note: Sets surface state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void EnvironmentRuntime::SetSurfaceState(SurfaceState state)
{
    surface_states_[state.surface_id] = state;
}

// Function note: Gets climate profile.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ClimateProfile EnvironmentRuntime::GetClimateProfile(RegionId region) const
{
    const auto iterator = climate_by_region_.find(region);
    if (iterator == climate_by_region_.end())
    {
        return ClimateProfile{};
    }

    return iterator->second;
}

// Function note: Handles apply drying.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void EnvironmentRuntime::ApplyDrying(std::int64_t game_delta_ticks)
{
    // Function note: Handles min.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const float dry_amount = std::min(1.0f, static_cast<float>(game_delta_ticks) * 0.0001f);

    for (auto& [surface_id, surface] : surface_states_)
    {
        (void)surface_id;
        if (surface.wetness <= 0.0f)
        {
            surface.wetness = 0.0f;
            if (surface.condition == SurfaceConditionKind::Wet || surface.condition == SurfaceConditionKind::Drying)
            {
                surface.condition = SurfaceConditionKind::Dry;
            }

            continue;
        }

        // Function note: Handles max.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        surface.wetness = std::max(0.0f, surface.wetness - dry_amount);
        if (surface.wetness == 0.0f)
        {
            surface.condition = SurfaceConditionKind::Dry;
        }
        // Function note: Handles if.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        else if (surface.condition == SurfaceConditionKind::Wet)
        {
            surface.condition = SurfaceConditionKind::Drying;
        }
    }
}
} 
