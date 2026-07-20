#pragma once

#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/environment_runtime.h"

#include <unordered_map>

namespace epidemic::runtime
{
class EnvironmentRuntime final : public IEnvironmentRuntime
{
  public:
    [[nodiscard]] foundation::Result<WeatherState> GetWeather(RegionId region) const override;
    [[nodiscard]] foundation::Result<SeasonState> GetSeason(RegionId region) const override;
    [[nodiscard]] foundation::Result<ClimateProfile> GetClimateProfile(RegionId region) const override;
    [[nodiscard]] foundation::Result<SurfaceState> GetSurfaceState(SurfaceId surface) const override;
    [[nodiscard]] foundation::Result<EnvironmentSnapshot> BuildSnapshot(RegionId region) const override;
    [[nodiscard]] foundation::Result<EnvironmentProjection> BuildProjection(RegionId region) const override;
    [[nodiscard]] std::uint64_t GetRevision() const override;
    [[nodiscard]] foundation::Result<std::uint64_t> GetRegionRevision(RegionId region) const override;

    [[nodiscard]] foundation::Result<void> RegisterRegionEnvironment(RegionId region, WeatherState weather, SeasonState season, ClimateProfile climate) override;
    [[nodiscard]] foundation::Result<void> SetWeather(RegionId region, WeatherState weather) override;
    [[nodiscard]] foundation::Result<void> SetSeason(RegionId region, SeasonState season) override;
    [[nodiscard]] foundation::Result<void> SetClimateProfile(RegionId region, ClimateProfile climate) override;
    [[nodiscard]] foundation::Result<void> SetSurfaceState(SurfaceState state) override;
    [[nodiscard]] foundation::Result<void> ApplyUpdate(const EnvironmentStateUpdate& update) override;
    [[nodiscard]] foundation::Result<void> Update(const EnvironmentUpdateInput& input) override;
    void SetUpdatePolicy(std::shared_ptr<const IEnvironmentUpdatePolicy> policy) override;

  private:
    [[nodiscard]] bool IsRegionRegistered(RegionId region) const;
    [[nodiscard]] foundation::Result<void> EnsureRegisteredRegion(RegionId region) const;
    [[nodiscard]] std::uint64_t LookupRegionRevision(RegionId region) const;
    [[nodiscard]] foundation::Result<std::uint64_t> AdvanceRevision();

    std::unordered_map<RegionId, WeatherState> weather_by_region_;
    std::unordered_map<RegionId, SeasonState> season_by_region_;
    std::unordered_map<RegionId, ClimateProfile> climate_by_region_;
    std::unordered_map<RegionId, std::uint64_t> revision_by_region_;
    std::unordered_map<SurfaceId, SurfaceState> surface_states_;
    std::shared_ptr<const IEnvironmentUpdatePolicy> update_policy_;
    std::uint64_t revision_ = 0;
};
} // namespace epidemic::runtime
