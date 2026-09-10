#include "environment_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string_view>
#include <unordered_set>
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

[[nodiscard]] bool IsValid(WeatherKind kind) noexcept
{
    switch (kind)
    {
    case WeatherKind::Clear:
    case WeatherKind::Cloudy:
    case WeatherKind::Rain:
    case WeatherKind::Storm:
    case WeatherKind::Snow:
    case WeatherKind::Fog:
    case WeatherKind::Transitioning:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValid(SeasonKind kind) noexcept
{
    switch (kind)
    {
    case SeasonKind::Spring:
    case SeasonKind::Summer:
    case SeasonKind::Autumn:
    case SeasonKind::Winter:
    case SeasonKind::Transitioning:
        return true;
    }
    return false;
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
    if (!IsValid(weather.kind))
    {
        return EnvironmentFailure("environment.invalid_weather_kind", "weather kind is outside the declared enum domain");
    }
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
    if (!IsValid(season.kind))
    {
        return EnvironmentFailure("environment.invalid_season_kind", "season kind is outside the declared enum domain");
    }
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
    // SurfaceConditionKind is a derived output. Caller-provided condition is intentionally ignored.
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
    return left.surface_id == right.surface_id && left.region_id == right.region_id && left.condition == right.condition &&
           left.wetness == right.wetness && left.snow_depth == right.snow_depth && left.mud_depth == right.mud_depth &&
           left.ice_thickness == right.ice_thickness && left.temperature == right.temperature;
}
} // namespace

foundation::Result<WeatherState> EnvironmentRuntime::GetWeather(RegionId region) const
{
    if (!region.IsValid())
    {
        return EnvironmentFailureValue<WeatherState>("environment.invalid_region", "region id must be valid");
    }
    const auto iterator = regions_.find(region);
    if (iterator == regions_.end())
    {
        return EnvironmentFailureValue<WeatherState>("environment.region_unknown", "weather is unknown for region");
    }
    return foundation::Result<WeatherState>::Success(iterator->second.weather);
}

foundation::Result<SeasonState> EnvironmentRuntime::GetSeason(RegionId region) const
{
    if (!region.IsValid())
    {
        return EnvironmentFailureValue<SeasonState>("environment.invalid_region", "region id must be valid");
    }
    const auto iterator = regions_.find(region);
    if (iterator == regions_.end())
    {
        return EnvironmentFailureValue<SeasonState>("environment.region_unknown", "season is unknown for region");
    }
    return foundation::Result<SeasonState>::Success(iterator->second.season);
}

foundation::Result<ClimateProfile> EnvironmentRuntime::GetClimateProfile(RegionId region) const
{
    if (!region.IsValid())
    {
        return EnvironmentFailureValue<ClimateProfile>("environment.invalid_region", "region id must be valid");
    }
    const auto iterator = regions_.find(region);
    if (iterator == regions_.end())
    {
        return EnvironmentFailureValue<ClimateProfile>("environment.region_unknown", "climate is unknown for region");
    }
    return foundation::Result<ClimateProfile>::Success(iterator->second.climate);
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
    const auto iterator = regions_.find(region);
    if (!region.IsValid())
    {
        return EnvironmentFailureValue<EnvironmentSnapshot>("environment.invalid_region", "region id must be valid");
    }
    if (iterator == regions_.end())
    {
        return EnvironmentFailureValue<EnvironmentSnapshot>("environment.region_unknown", "environment is unknown for region");
    }

    EnvironmentSnapshot snapshot{};
    snapshot.region_id = region;
    snapshot.revision = iterator->second.revision;
    snapshot.weather = iterator->second.weather;
    snapshot.season = iterator->second.season;
    snapshot.climate = iterator->second.climate;
    snapshot.temperature = iterator->second.weather.current_temperature;
    snapshot.humidity = iterator->second.weather.current_humidity;
    for (const auto& [surface_id, surface] : surface_states_)
    {
        (void)surface_id;
        if (surface.region_id == region)
        {
            snapshot.surfaces.push_back(surface);
        }
    }
    std::sort(snapshot.surfaces.begin(), snapshot.surfaces.end(), [](const SurfaceState& left, const SurfaceState& right) {
        return left.surface_id.value < right.surface_id.value;
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
    return foundation::Result<EnvironmentProjection>::Success(EnvironmentProjection{
        region,
        snapshot.Value().revision,
        snapshot.Value().weather,
        snapshot.Value().season,
        snapshot.Value().temperature,
        snapshot.Value().humidity});
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
    if (registration_frozen_)
    {
        return EnvironmentFailure("environment.registration_frozen", "region environment registration is frozen");
    }
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
    const auto next_revision = PeekNextRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    weather.wind_direction_degrees = NormalizeWindDirection(weather.wind_direction_degrees);
    if (ConsumeAllocationFailureForTesting())
    {
        return EnvironmentFailure("environment.allocation_failed", "region environment allocation failed");
    }
    try
    {
        const auto [_, inserted] = regions_.emplace(region, RegionEnvironmentRecord{weather, season, climate, next_revision.Value()});
        if (!inserted)
        {
            return EnvironmentFailure("environment.region_already_registered", "region environment is already registered");
        }
    }
    catch (const std::exception&)
    {
        return EnvironmentFailure("environment.allocation_failed", "region environment allocation failed");
    }
    revision_ = next_revision.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetWeather(RegionId region, WeatherState weather)
{
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
    RegionEnvironmentRecord& record = regions_.at(region);
    if (record.weather == weather)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = PeekNextRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    record.weather = weather;
    record.revision = next_revision.Value();
    revision_ = next_revision.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetSeason(RegionId region, SeasonState season)
{
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
    RegionEnvironmentRecord& record = regions_.at(region);
    if (record.season == season)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = PeekNextRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    record.season = season;
    record.revision = next_revision.Value();
    revision_ = next_revision.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentRuntime::SetClimateProfile(RegionId region, ClimateProfile climate)
{
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
    RegionEnvironmentRecord& record = regions_.at(region);
    if (record.climate == climate)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = PeekNextRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    record.climate = climate;
    record.revision = next_revision.Value();
    revision_ = next_revision.Value();
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
    const auto existing = surface_states_.find(state.surface_id);
    const auto ownership = ValidateSurfaceMutation(existing == surface_states_.end() ? nullptr : &existing->second, state);
    if (!ownership)
    {
        return ownership;
    }
    if (existing != surface_states_.end() && SameSurfaceStateIgnoringRevision(existing->second, state))
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = PeekNextRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    state.revision = next_revision.Value();

    if (ConsumeAllocationFailureForTesting())
    {
        return EnvironmentFailure("environment.allocation_failed", "surface state allocation failed");
    }
    try
    {
        auto staged = surface_states_;
        staged[state.surface_id] = state;
        surface_states_.swap(staged);
    }
    catch (const std::exception&)
    {
        return EnvironmentFailure("environment.allocation_failed", "surface state allocation failed");
    }
    RegionEnvironmentRecord& region = regions_.at(state.region_id);
    region.revision = next_revision.Value();
    revision_ = next_revision.Value();
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
    const RegionEnvironmentRecord& current_region = regions_.at(update.region);
    if (update.source_revision != current_region.revision)
    {
        return EnvironmentFailure("environment.revision_conflict", "environment update source revision does not match current region revision");
    }

    RegionEnvironmentRecord staged_region = current_region;
    if (update.weather)
    {
        staged_region.weather = *update.weather;
        const auto valid = ValidateWeather(staged_region.weather);
        if (!valid)
        {
            return valid;
        }
        staged_region.weather.wind_direction_degrees = NormalizeWindDirection(staged_region.weather.wind_direction_degrees);
    }
    if (update.season)
    {
        staged_region.season = *update.season;
        const auto valid = ValidateSeason(staged_region.season);
        if (!valid)
        {
            return valid;
        }
    }
    if (update.climate)
    {
        staged_region.climate = *update.climate;
        const auto valid = ValidateClimate(staged_region.climate);
        if (!valid)
        {
            return valid;
        }
    }

    std::unordered_set<SurfaceId> seen;
    std::vector<SurfaceState> staged_surface_updates;
    try
    {
        seen.reserve(update.surfaces.size());
        staged_surface_updates.reserve(update.surfaces.size());
        for (SurfaceState surface : update.surfaces)
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
            if (!seen.insert(surface.surface_id).second)
            {
                return EnvironmentFailure("environment.duplicate_surface_update", "environment batch contains duplicate surface ids");
            }
            surface.condition = DeriveSurfaceCondition(surface);
            const auto existing = surface_states_.find(surface.surface_id);
            const auto ownership = ValidateSurfaceMutation(existing == surface_states_.end() ? nullptr : &existing->second, surface);
            if (!ownership)
            {
                return ownership;
            }
            staged_surface_updates.push_back(surface);
        }
    }
    catch (const std::exception&)
    {
        return EnvironmentFailure("environment.allocation_failed", "environment batch validation allocation failed");
    }

    bool changed = staged_region.weather != current_region.weather || staged_region.season != current_region.season ||
                   staged_region.climate != current_region.climate;
    for (const SurfaceState& surface : staged_surface_updates)
    {
        const auto existing = surface_states_.find(surface.surface_id);
        if (existing == surface_states_.end() || !SameSurfaceStateIgnoringRevision(existing->second, surface))
        {
            changed = true;
            break;
        }
    }
    if (!changed)
    {
        return foundation::Result<void>::Success();
    }

    const auto next_revision = PeekNextRevision();
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(next_revision.GetError());
    }
    staged_region.revision = next_revision.Value();
    for (SurfaceState& surface : staged_surface_updates)
    {
        surface.revision = next_revision.Value();
    }

    if (ConsumeAllocationFailureForTesting())
    {
        return EnvironmentFailure("environment.allocation_failed", "environment batch allocation failed");
    }
    try
    {
        auto staged_surfaces = surface_states_;
        for (const SurfaceState& surface : staged_surface_updates)
        {
            staged_surfaces[surface.surface_id] = surface;
        }
        surface_states_.swap(staged_surfaces);
    }
    catch (const std::exception&)
    {
        return EnvironmentFailure("environment.allocation_failed", "environment batch allocation failed");
    }

    regions_.at(update.region) = staged_region;
    revision_ = next_revision.Value();
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
    if (input.game_delta.ticks == 0 || update_policy_ == nullptr)
    {
        return foundation::Result<void>::Success();
    }

    try
    {
        const auto update = update_policy_->BuildUpdate(input, *this);
        if (!update)
        {
            return foundation::Result<void>::Failure(update.GetError());
        }
        return ApplyUpdate(update.Value());
    }
    catch (const std::exception&)
    {
        return EnvironmentFailure("environment.update_policy_exception", "environment update policy threw an exception");
    }
    catch (...)
    {
        return EnvironmentFailure("environment.update_policy_exception", "environment update policy threw an unknown exception");
    }
}

void EnvironmentRuntime::SetUpdatePolicy(std::shared_ptr<const IEnvironmentUpdatePolicy> policy)
{
    update_policy_ = std::move(policy);
}

void EnvironmentRuntime::FreezeRegistration() noexcept
{
    registration_frozen_ = true;
}

bool EnvironmentRuntime::IsRegistrationFrozen() const noexcept
{
    return registration_frozen_;
}

void EnvironmentRuntime::SetRevisionForTesting(std::uint64_t revision) noexcept
{
    revision_ = revision;
}

void EnvironmentRuntime::FailNextAllocationForTesting() noexcept
{
    fail_next_allocation_for_testing_ = true;
}

bool EnvironmentRuntime::IsRegionRegistered(RegionId region) const
{
    return regions_.contains(region);
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
    const auto iterator = regions_.find(region);
    return iterator == regions_.end() ? 0u : iterator->second.revision;
}

foundation::Result<std::uint64_t> EnvironmentRuntime::PeekNextRevision() const
{
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return EnvironmentFailureValue<std::uint64_t>("environment.revision_overflow", "environment revision cannot advance beyond UINT64_MAX");
    }
    return foundation::Result<std::uint64_t>::Success(revision_ + 1u);
}

foundation::Result<void> EnvironmentRuntime::ValidateSurfaceMutation(const SurfaceState* existing, const SurfaceState& candidate) const
{
    if (existing != nullptr && existing->region_id != candidate.region_id)
    {
        return EnvironmentFailure("environment.surface_region_mismatch", "surface ownership cannot change regions");
    }
    return foundation::Result<void>::Success();
}

bool EnvironmentRuntime::ConsumeAllocationFailureForTesting() noexcept
{
    if (!fail_next_allocation_for_testing_)
    {
        return false;
    }
    fail_next_allocation_for_testing_ = false;
    return true;
}
} // namespace epidemic::runtime
