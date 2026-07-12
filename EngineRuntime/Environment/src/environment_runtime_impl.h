#pragma once

#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/environment_runtime.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <unordered_map>

namespace epidemic::runtime
{
class EnvironmentRuntime final : public IEnvironmentRuntime
{
  public:
    // Function note: Gets weather.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] WeatherState GetWeather(RegionId region) const override;
    // Function note: Gets season.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] SeasonState GetSeason(RegionId region) const override;
    // Function note: Gets surface state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::optional<SurfaceState> GetSurfaceState(SurfaceId surface) const override;

    // Function note: Builds snapshot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] EnvironmentSnapshot BuildSnapshot(RegionId region) const override;
    // Function note: Builds projection.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] EnvironmentProjection BuildProjection(RegionId region) const override;
    // Function note: Updates the associated runtime state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> Update(const EnvironmentUpdateInput& input) override;

    // Function note: Sets weather.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void SetWeather(RegionId region, WeatherState weather);
    // Function note: Sets season.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void SetSeason(RegionId region, SeasonState season);
    // Function note: Sets climate profile.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void SetClimateProfile(RegionId region, ClimateProfile climate);
    // Function note: Sets surface state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void SetSurfaceState(SurfaceState state);

  private:
    // Function note: Gets climate profile.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] ClimateProfile GetClimateProfile(RegionId region) const;
    // Function note: Handles apply drying.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void ApplyDrying(std::int64_t game_delta_ticks);

    std::unordered_map<RegionId, WeatherState> weather_by_region_;
    std::unordered_map<RegionId, SeasonState> season_by_region_;
    std::unordered_map<RegionId, ClimateProfile> climate_by_region_;
    std::unordered_map<SurfaceId, SurfaceState> surface_states_;
};
} // namespace epidemic::runtime
