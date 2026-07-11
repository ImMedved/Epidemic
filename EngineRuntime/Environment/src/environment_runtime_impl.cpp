#include "environment_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime
{
WeatherState EnvironmentRuntime::GetWeather(RegionId region) const
{
    const auto iterator = weather_by_region_.find(region);
    if (iterator == weather_by_region_.end())
    {
        return WeatherState{};
    }

    return iterator->second;
}

SeasonState EnvironmentRuntime::GetSeason(RegionId region) const
{
    const auto iterator = season_by_region_.find(region);
    if (iterator == season_by_region_.end())
    {
        return SeasonState{};
    }

    return iterator->second;
}

std::optional<SurfaceState> EnvironmentRuntime::GetSurfaceState(SurfaceId surface) const
{
    const auto iterator = surface_states_.find(surface);
    if (iterator == surface_states_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

EnvironmentSnapshot EnvironmentRuntime::BuildSnapshot(RegionId region) const
{
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

EnvironmentProjection EnvironmentRuntime::BuildProjection(RegionId region) const
{
    const EnvironmentSnapshot snapshot = BuildSnapshot(region);
    return EnvironmentProjection{snapshot.region_id, snapshot.weather, snapshot.season, snapshot.temperature, snapshot.humidity};
}

foundation::Result<void> EnvironmentRuntime::Update(const EnvironmentUpdateInput& input)
{
    if (!input.region_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("environment.invalid_region", "environment update requires a valid region id"));
    }

    if (input.game_delta_ticks <= 0)
    {
        return foundation::Result<void>::Success();
    }

    ApplyDrying(input.game_delta_ticks);
    return foundation::Result<void>::Success();
}

void EnvironmentRuntime::SetWeather(RegionId region, WeatherState weather)
{
    weather_by_region_[region] = weather;
}

void EnvironmentRuntime::SetSeason(RegionId region, SeasonState season)
{
    season_by_region_[region] = season;
}

void EnvironmentRuntime::SetClimateProfile(RegionId region, ClimateProfile climate)
{
    climate_by_region_[region] = climate;
}

void EnvironmentRuntime::SetSurfaceState(SurfaceState state)
{
    surface_states_[state.surface_id] = state;
}

ClimateProfile EnvironmentRuntime::GetClimateProfile(RegionId region) const
{
    const auto iterator = climate_by_region_.find(region);
    if (iterator == climate_by_region_.end())
    {
        return ClimateProfile{};
    }

    return iterator->second;
}

void EnvironmentRuntime::ApplyDrying(std::int64_t game_delta_ticks)
{
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

        surface.wetness = std::max(0.0f, surface.wetness - dry_amount);
        if (surface.wetness == 0.0f)
        {
            surface.condition = SurfaceConditionKind::Dry;
        }
        else if (surface.condition == SurfaceConditionKind::Wet)
        {
            surface.condition = SurfaceConditionKind::Drying;
        }
    }
}
} // namespace epidemic::runtime
