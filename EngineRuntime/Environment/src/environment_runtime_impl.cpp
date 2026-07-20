#include "environment_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

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
    if (!Unit(weather.intensity) || !Unit(weather.cloudiness) || !Unit(weather.precipitation) ||
        !std::isfinite(weather.current_temperature) || !Unit(weather.current_humidity) || !NonNegative(weather.wind_speed) ||
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

[[nodiscard]] float NormalizeWindDirection(float degrees) noexcept
{
    float normalized = std::fmod(degrees, 360.0f);
    if (normalized < 0.0f)
    {
        normalized += 360.0f;
    }
    return normalized;
}

[[nodiscard]] bool SameSurfaceStateIgnoringRevision(const SurfaceState& left, const SurfaceState& right) noexcept
{
    return left.surface_id == right.surface_id &&
           left.region_id == right.region_id &&
           left.condition == right.condition &&
           left.wetness == right.wetness &&
           left.snow_depth == right.snow_depth &&
           left.mud_depth == right.mud_depth &&
           left.ice_thickness == right.ice_thickness &&
           left.temperature == right.temperature;
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
    snapshot.revision = LookupRegionRevision(region);
    snapshot.weather = weather.Value();
    snapshot.season = season.Value();
    snapshot.climate = climate.Value();
    snapshot.temperature = weather.Value().current_temperature;
    snapshot.humidity = weather.Value().current_humidity;
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

foundation::Result<std::uint64_t> EnvironmentRuntime::GetRegionRevision(RegionId region) const
{
    const auto registered = EnsureRegisteredRegion(region);
    if (!registered)
    {
        return foundation::Result<std::uint64_t>::Failure(registered.GetError());
    }
    return foundation::Result<std::uint64_t>::Success(LookupRegionRevision(region));
}

foundation::Result<void> EnvironmentRuntime::RegisterRegionEnvironment(RegionId region, WeatherState weather, SeasonState season, ClimateProfile climate)
{
    if (!region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "region registration requires a valid region id");
    }
    if (IsRegionRegistered(region))
    {
        return EnvironmentFailure("environment.region_already_registered", "region environment is already registered");
    }
    const auto valid_weather = ValidateWeather(weather);
    if (!valid_weather)
    {
        return valid_weather;
    }
    const auto valid_season = ValidateSeason(season);
    if (!valid_season)
    {
        return valid_season;
    }
    const auto valid_climate = ValidateClimate(climate);
    if (!valid_climate)
    {
        return valid_climate;
    }

    weather.wind_direction_degrees = NormalizeWindDirection(weather.wind_direction_degrees);
    const auto next_revision = AdvanceRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    weather_by_region_[region] = weather;
    season_by_region_[region] = season;
    climate_by_region_[region] = climate;
    revision_by_region_[region] = next_revision.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetWeather(RegionId region, WeatherState weather)
{
    if (!region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "weather update requires a valid region id");
    }
    const auto registered = EnsureRegisteredRegion(region);
    if (!registered)
    {
        return registered;
    }
    const auto valid = ValidateWeather(weather);
    if (!valid)
    {
        return valid;
    }
    weather.wind_direction_degrees = NormalizeWindDirection(weather.wind_direction_degrees);
    if (weather_by_region_[region] == weather)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = AdvanceRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    weather_by_region_[region] = weather;
    revision_by_region_[region] = next_revision.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetSeason(RegionId region, SeasonState season)
{
    if (!region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "season update requires a valid region id");
    }
    const auto registered = EnsureRegisteredRegion(region);
    if (!registered)
    {
        return registered;
    }
    const auto valid = ValidateSeason(season);
    if (!valid)
    {
        return valid;
    }
    if (season_by_region_[region] == season)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = AdvanceRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    season_by_region_[region] = season;
    revision_by_region_[region] = next_revision.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetClimateProfile(RegionId region, ClimateProfile climate)
{
    if (!region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "climate update requires a valid region id");
    }
    const auto registered = EnsureRegisteredRegion(region);
    if (!registered)
    {
        return registered;
    }
    const auto valid = ValidateClimate(climate);
    if (!valid)
    {
        return valid;
    }
    if (climate_by_region_[region] == climate)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = AdvanceRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    climate_by_region_[region] = climate;
    revision_by_region_[region] = next_revision.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetSurfaceState(SurfaceState state)
{
    const auto valid = ValidateSurface(state);
    if (!valid)
    {
        return valid;
    }
    const auto registered = EnsureRegisteredRegion(state.region_id);
    if (!registered)
    {
        return registered;
    }
    state.condition = DeriveSurfaceCondition(state);
    if (const auto existing = surface_states_.find(state.surface_id); existing != surface_states_.end())
    {
        if (existing->second.region_id != state.region_id)
        {
            return EnvironmentFailure("environment.surface_region_mismatch", "surface ownership cannot change regions through SetSurfaceState");
        }
        if (SameSurfaceStateIgnoringRevision(existing->second, state))
        {
            return foundation::Result<void>::Success();
        }
    }
    const auto next_revision = AdvanceRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    state.revision = next_revision.Value();
    revision_by_region_[state.region_id] = state.revision;
    surface_states_[state.surface_id] = state;
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::ApplyUpdate(const EnvironmentStateUpdate& update)
{
    if (!update.region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "environment update batch requires a valid region id");
    }
    const auto registered = EnsureRegisteredRegion(update.region);
    if (!registered)
    {
        return registered;
    }
    if (update.source_revision != LookupRegionRevision(update.region))
    {
        return EnvironmentFailure("environment.revision_conflict", "environment update source revision does not match current region revision");
    }

    WeatherState weather{};
    bool changed = update.weather.has_value() || update.season.has_value() || update.climate.has_value() || !update.surfaces.empty();
    if (!changed)
    {
        return foundation::Result<void>::Success();
    }
    if (update.weather)
    {
        weather = *update.weather;
        const auto valid = ValidateWeather(weather);
        if (!valid)
        {
            return valid;
        }
        weather.wind_direction_degrees = NormalizeWindDirection(weather.wind_direction_degrees);
    }
    if (update.season)
    {
        const auto valid = ValidateSeason(*update.season);
        if (!valid)
        {
            return valid;
        }
    }
    if (update.climate)
    {
        const auto valid = ValidateClimate(*update.climate);
        if (!valid)
        {
            return valid;
        }
    }
    for (const SurfaceState& surface : update.surfaces)
    {
        const auto valid = ValidateSurface(surface);
        if (!valid)
        {
            return valid;
        }
        if (surface.region_id != update.region)
        {
            return EnvironmentFailure("environment.invalid_region", "surface update must target the batch region");
        }
    }

    const auto next_revision = AdvanceRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    const std::uint64_t region_revision = next_revision.Value();
    revision_by_region_[update.region] = region_revision;
    if (update.weather)
    {
        weather_by_region_[update.region] = weather;
    }
    if (update.season)
    {
        season_by_region_[update.region] = *update.season;
    }
    if (update.climate)
    {
        climate_by_region_[update.region] = *update.climate;
    }
    for (SurfaceState surface : update.surfaces)
    {
        surface.condition = DeriveSurfaceCondition(surface);
        surface.revision = region_revision;
        surface_states_[surface.surface_id] = surface;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::Update(const EnvironmentUpdateInput& input)
{
    if (!input.region_id.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "environment update requires a valid region id");
    }
    if (input.game_delta.ticks < 0)
    {
        return EnvironmentFailure("environment.invalid_delta", "environment update delta must not be negative");
    }
    const auto registered = EnsureRegisteredRegion(input.region_id);
    if (!registered)
    {
        return registered;
    }
    if (input.game_delta.ticks == 0)
    {
        return foundation::Result<void>::Success();
    }
    if (update_policy_ == nullptr)
    {
        return foundation::Result<void>::Success();
    }

    const auto update = update_policy_->BuildUpdate(input, *this);
    if (!update)
    {
        return foundation::Result<void>::Failure(update.GetError());
    }
    return ApplyUpdate(update.Value());
}

void EnvironmentRuntime::SetUpdatePolicy(std::shared_ptr<const IEnvironmentUpdatePolicy> policy)
{
    update_policy_ = std::move(policy);
}

bool EnvironmentRuntime::IsRegionRegistered(RegionId region) const
{
    return weather_by_region_.contains(region) && season_by_region_.contains(region) && climate_by_region_.contains(region) &&
           revision_by_region_.contains(region);
}

foundation::Result<void> EnvironmentRuntime::EnsureRegisteredRegion(RegionId region) const
{
    if (!region.IsValid())
    {
        return EnvironmentFailure("environment.invalid_region", "region id must be valid");
    }
    if (!IsRegionRegistered(region))
    {
        return EnvironmentFailure("environment.region_unknown", "region environment is not registered");
    }
    return foundation::Result<void>::Success();
}

std::uint64_t EnvironmentRuntime::LookupRegionRevision(RegionId region) const
{
    const auto iterator = revision_by_region_.find(region);
    return iterator == revision_by_region_.end() ? 0u : iterator->second;
}

foundation::Result<std::uint64_t> EnvironmentRuntime::AdvanceRevision()
{
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return EnvironmentFailureValue<std::uint64_t>("environment.revision_overflow", "environment revision cannot advance beyond UINT64_MAX");
    }
    ++revision_;
    return foundation::Result<std::uint64_t>::Success(revision_);
}
} // namespace epidemic::runtime
