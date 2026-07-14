#pragma once

#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/season_state.h"
#include "Epidemic/Runtime/Environment/surface_state.h"
#include "Epidemic/Runtime/Environment/weather_state.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

#include <cstdint>
#include <vector>

namespace epidemic::runtime
{
struct EnvironmentSnapshot
{
    RegionId region_id{};
    std::uint64_t revision = 0;
    WeatherState weather{};
    SeasonState season{};
    ClimateProfile climate{};
    float temperature = 0.0f;
    float humidity = 0.0f;
    std::vector<SurfaceState> surfaces;

    [[nodiscard]] bool operator==(const EnvironmentSnapshot&) const = default;
};
} // namespace epidemic::runtime
