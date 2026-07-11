#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Environment/environment_projection.h"
#include "Epidemic/Runtime/Environment/environment_snapshot.h"
#include "Epidemic/Runtime/Environment/environment_update.h"
#include "Epidemic/Runtime/Environment/season_state.h"
#include "Epidemic/Runtime/Environment/surface_state.h"
#include "Epidemic/Runtime/Environment/weather_state.h"

#include <optional>

namespace epidemic::runtime
{
class IEnvironmentRuntime
{
  public:
    virtual ~IEnvironmentRuntime() = default;

    [[nodiscard]] virtual WeatherState GetWeather(RegionId region) const = 0;
    [[nodiscard]] virtual SeasonState GetSeason(RegionId region) const = 0;
    [[nodiscard]] virtual std::optional<SurfaceState> GetSurfaceState(SurfaceId surface) const = 0;

    [[nodiscard]] virtual EnvironmentSnapshot BuildSnapshot(RegionId region) const = 0;
    [[nodiscard]] virtual EnvironmentProjection BuildProjection(RegionId region) const = 0;
    [[nodiscard]] virtual foundation::Result<void> Update(const EnvironmentUpdateInput& input) = 0;
};
} // namespace epidemic::runtime
