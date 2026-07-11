#pragma once

#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/season_state.h"
#include "Epidemic/Runtime/Environment/weather_state.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

namespace epidemic::runtime
{
struct EnvironmentSnapshot
{
    RegionId region_id{};
    WeatherState weather{};
    SeasonState season{};
    ClimateProfile climate{};
    float temperature = 0.0f;
    float humidity = 0.0f;

    [[nodiscard]] constexpr bool operator==(const EnvironmentSnapshot&) const noexcept = default;
};
} // namespace epidemic::runtime
