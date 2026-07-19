#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/environment_projection.h"
#include "Epidemic/Runtime/Environment/environment_snapshot.h"
#include "Epidemic/Runtime/Environment/environment_update.h"
#include "Epidemic/Runtime/Environment/season_state.h"
#include "Epidemic/Runtime/Environment/surface_state.h"
#include "Epidemic/Runtime/Environment/weather_state.h"

#include <cstdint>
#include <memory>

namespace epidemic::runtime
{
class IEnvironmentQuery
{
  public:
    virtual ~IEnvironmentQuery() = default;

    [[nodiscard]] virtual foundation::Result<WeatherState> GetWeather(RegionId region) const = 0;
    [[nodiscard]] virtual foundation::Result<SeasonState> GetSeason(RegionId region) const = 0;
    [[nodiscard]] virtual foundation::Result<ClimateProfile> GetClimateProfile(RegionId region) const = 0;
    [[nodiscard]] virtual foundation::Result<SurfaceState> GetSurfaceState(SurfaceId surface) const = 0;
    [[nodiscard]] virtual foundation::Result<EnvironmentSnapshot> BuildSnapshot(RegionId region) const = 0;
    [[nodiscard]] virtual foundation::Result<EnvironmentProjection> BuildProjection(RegionId region) const = 0;
    [[nodiscard]] virtual std::uint64_t GetRevision() const = 0;
    [[nodiscard]] virtual foundation::Result<std::uint64_t> GetRegionRevision(RegionId region) const = 0;
};

// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IEnvironmentWriter
{
  public:
    virtual ~IEnvironmentWriter() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterRegionEnvironment(
        RegionId region,
        WeatherState weather,
        SeasonState season,
        ClimateProfile climate) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetWeather(RegionId region, WeatherState weather) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetSeason(RegionId region, SeasonState season) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetClimateProfile(RegionId region, ClimateProfile climate) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetSurfaceState(SurfaceState state) = 0;
    [[nodiscard]] virtual foundation::Result<void> ApplyUpdate(const EnvironmentStateUpdate& update) = 0;
    [[nodiscard]] virtual foundation::Result<void> Update(const EnvironmentUpdateInput& input) = 0;
};

class IEnvironmentUpdatePolicy
{
  public:
    virtual ~IEnvironmentUpdatePolicy() = default;

    [[nodiscard]] virtual foundation::Result<EnvironmentStateUpdate> BuildUpdate(
        const EnvironmentUpdateInput& input,
        const IEnvironmentQuery& query) const = 0;
};

class IEnvironmentRuntime : public IEnvironmentQuery, public IEnvironmentWriter
{
  public:
    ~IEnvironmentRuntime() override = default;

    virtual void SetUpdatePolicy(std::shared_ptr<const IEnvironmentUpdatePolicy> policy) = 0;
};
} // namespace epidemic::runtime
