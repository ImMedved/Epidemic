#include "environment_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> EnvironmentFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> EnvironmentFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(foundation::Error::Create(code, message));
}

[[nodiscard]] bool Unit(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

[[nodiscard]] bool NonNegative(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0f;
}

[[nodiscard]] foundation::Result<void> ValidateWeather(WeatherState weather)
{
    if (!Unit(weather.intensity) || !Unit(weather.cloudiness) || !Unit(weather.precipitation) || !NonNegative(weather.wind_speed) ||
        !std::isfinite(weather.wind_direction_degrees))
    {
        return EnvironmentFailure("environment.invalid_weather", "weather values must be finite and normalized where required");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateSeason(SeasonState season)
{
    if (!Unit(season.progress))
    {
        return EnvironmentFailure("environment.invalid_season", "season progress must be in [0, 1]");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateClimate(ClimateProfile climate)
{
    if (!std::isfinite(climate.average_temperature) || !Unit(climate.average_humidity) || !NonNegative(climate.average_wind_speed) ||
        !NonNegative(climate.annual_precipitation))
    {
        return EnvironmentFailure("environment.invalid_climate", "climate values must be finite and normalized where required");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateSurface(SurfaceState surface)
{
    if (!surface.surface_id.IsValid())
    {
        return EnvironmentFailure("environment.invalid_surface", "surface id must be valid");
    }
    if (!surface.region_id.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "surface state must declare a valid region id");
    }
    if (!Unit(surface.wetness) || !NonNegative(surface.snow_depth) || !NonNegative(surface.mud_depth) ||
        !NonNegative(surface.ice_thickness) || !std::isfinite(surface.temperature))
    {
        return EnvironmentFailure("environment.invalid_surface_state", "surface wet/snow/mud/ice values must be finite and valid");
    }
    return foundation::Result<void>::Success();
}
} // namespace

foundation::Result<WeatherState> EnvironmentRuntime::GetWeather(RegionId region) const
{
    if (!region.IsValid())
    {
        return EnvironmentFailureValue<WeatherState>("environment.invalid_region", "region id must be valid");
    }
    const auto iterator = weather_by_region_.find(region);
    if (iterator == weather_by_region_.end())
    {
        return EnvironmentFailureValue<WeatherState>("environment.region_unknown", "weather is unknown for region");
    }
    return foundation::Result<WeatherState>::Success(iterator->second);
}

foundation::Result<SeasonState> EnvironmentRuntime::GetSeason(RegionId region) const
{
    if (!region.IsValid())
    {
        return EnvironmentFailureValue<SeasonState>("environment.invalid_region", "region id must be valid");
    }
    const auto iterator = season_by_region_.find(region);
    if (iterator == season_by_region_.end())
    {
        return EnvironmentFailureValue<SeasonState>("environment.region_unknown", "season is unknown for region");
    }
    return foundation::Result<SeasonState>::Success(iterator->second);
}

foundation::Result<ClimateProfile> EnvironmentRuntime::GetClimateProfile(RegionId region) const
{
    if (!region.IsValid())
    {
        return EnvironmentFailureValue<ClimateProfile>("environment.invalid_region", "region id must be valid");
    }
    const auto iterator = climate_by_region_.find(region);
    if (iterator == climate_by_region_.end())
    {
        return EnvironmentFailureValue<ClimateProfile>("environment.region_unknown", "climate is unknown for region");
    }
    return foundation::Result<ClimateProfile>::Success(iterator->second);
}

foundation::Result<SurfaceState> EnvironmentRuntime::GetSurfaceState(SurfaceId surface) const
{
    if (!surface.IsValid())
    {
        return EnvironmentFailureValue<SurfaceState>("environment.invalid_surface", "surface id must be valid");
    }
    const auto iterator = surface_states_.find(surface);
    if (iterator == surface_states_.end())
    {
        return EnvironmentFailureValue<SurfaceState>("environment.surface_unknown", "surface state is unknown");
    }
    return foundation::Result<SurfaceState>::Success(iterator->second);
}

foundation::Result<EnvironmentSnapshot> EnvironmentRuntime::BuildSnapshot(RegionId region) const
{
    const auto weather = GetWeather(region);
    if (!weather)
    {
        return foundation::Result<EnvironmentSnapshot>::Failure(weather.GetError());
    }
    const auto season = GetSeason(region);
    if (!season)
    {
        return foundation::Result<EnvironmentSnapshot>::Failure(season.GetError());
    }
    const auto climate = GetClimateProfile(region);
    if (!climate)
    {
        return foundation::Result<EnvironmentSnapshot>::Failure(climate.GetError());
    }

    EnvironmentSnapshot snapshot{};
    snapshot.region_id = region;
    snapshot.revision = revision_;
    snapshot.weather = weather.Value();
    snapshot.season = season.Value();
    snapshot.climate = climate.Value();
    snapshot.temperature = climate.Value().average_temperature;
    snapshot.humidity = climate.Value().average_humidity;
    for (const auto& [surface_id, surface] : surface_states_)
    {
        (void)surface_id;
        if (surface.region_id == region)
        {
            snapshot.surfaces.push_back(surface);
        }
    }
    std::sort(snapshot.surfaces.begin(), snapshot.surfaces.end(), [](const auto& left, const auto& right) {
        return left.surface_id.Raw() < right.surface_id.Raw();
    });
    return foundation::Result<EnvironmentSnapshot>::Success(std::move(snapshot));
}

foundation::Result<EnvironmentProjection> EnvironmentRuntime::BuildProjection(RegionId region) const
{
    const auto snapshot = BuildSnapshot(region);
    if (!snapshot)
    {
        return foundation::Result<EnvironmentProjection>::Failure(snapshot.GetError());
    }
    return foundation::Result<EnvironmentProjection>::Success(EnvironmentProjection{snapshot.Value().region_id, snapshot.Value().revision,
                                                                                   snapshot.Value().weather, snapshot.Value().season,
                                                                                   snapshot.Value().temperature, snapshot.Value().humidity});
}

std::uint64_t EnvironmentRuntime::GetRevision() const
{
    return revision_;
}

foundation::Result<void> EnvironmentRuntime::SetWeather(RegionId region, WeatherState weather)
{
    if (!region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "weather update requires a valid region id");
    }
    const auto valid = ValidateWeather(weather);
    if (!valid)
    {
        return valid;
    }
    weather_by_region_[region] = weather;
    BumpRevision();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetSeason(RegionId region, SeasonState season)
{
    if (!region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "season update requires a valid region id");
    }
    const auto valid = ValidateSeason(season);
    if (!valid)
    {
        return valid;
    }
    season_by_region_[region] = season;
    BumpRevision();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetClimateProfile(RegionId region, ClimateProfile climate)
{
    if (!region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "climate update requires a valid region id");
    }
    const auto valid = ValidateClimate(climate);
    if (!valid)
    {
        return valid;
    }
    climate_by_region_[region] = climate;
    BumpRevision();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetSurfaceState(SurfaceState state)
{
    const auto valid = ValidateSurface(state);
    if (!valid)
    {
        return valid;
    }
    BumpRevision();
    state.revision = revision_;
    surface_states_[state.surface_id] = state;
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::Update(const EnvironmentUpdateInput& input)
{
    if (!input.region_id.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "environment update requires a valid region id");
    }
    if (input.game_delta_ticks < 0)
    {
        return EnvironmentFailure("environment.invalid_delta", "environment update delta must not be negative");
    }
    if (weather_by_region_.find(input.region_id) == weather_by_region_.end() &&
        season_by_region_.find(input.region_id) == season_by_region_.end() && climate_by_region_.find(input.region_id) == climate_by_region_.end())
    {
        return EnvironmentFailure("environment.region_unknown", "environment update region is unknown");
    }
    if (update_policy_ == nullptr || input.game_delta_ticks == 0)
    {
        return foundation::Result<void>::Success();
    }
    return update_policy_->Apply(input, *this, *this);
}

void EnvironmentRuntime::SetUpdatePolicy(IEnvironmentUpdatePolicy* policy)
{
    update_policy_ = policy;
}

void EnvironmentRuntime::BumpRevision()
{
    ++revision_;
}
} // namespace epidemic::runtime
