#pragma once

#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/environment_runtime.h"

#include <unordered_map>

namespace epidemic::runtime
{
class EnvironmentRuntime final : public IEnvironmentRuntime
{
  public:
    [[nodiscard]] WeatherState GetWeather(RegionId region) const override;
    [[nodiscard]] SeasonState GetSeason(RegionId region) const override;
    [[nodiscard]] std::optional<SurfaceState> GetSurfaceState(SurfaceId surface) const override;

    [[nodiscard]] EnvironmentSnapshot BuildSnapshot(RegionId region) const override;
    [[nodiscard]] EnvironmentProjection BuildProjection(RegionId region) const override;
    [[nodiscard]] foundation::Result<void> Update(const EnvironmentUpdateInput& input) override;

    void SetWeather(RegionId region, WeatherState weather);
    void SetSeason(RegionId region, SeasonState season);
    void SetClimateProfile(RegionId region, ClimateProfile climate);
    void SetSurfaceState(SurfaceState state);

  private:
    [[nodiscard]] ClimateProfile GetClimateProfile(RegionId region) const;
    void ApplyDrying(std::int64_t game_delta_ticks);

    std::unordered_map<RegionId, WeatherState> weather_by_region_;
    std::unordered_map<RegionId, SeasonState> season_by_region_;
    std::unordered_map<RegionId, ClimateProfile> climate_by_region_;
    std::unordered_map<SurfaceId, SurfaceState> surface_states_;
};
} // namespace epidemic::runtime
