#pragma once

#include "Epidemic/Runtime/Environment/season_state.h"
#include "Epidemic/Runtime/Environment/weather_state.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

namespace epidemic::runtime
{
struct EnvironmentProjection
{
    RegionId region_id{};
    WeatherState weather{};
    SeasonState season{};
    float temperature = 0.0f;
    float humidity = 0.0f;

    [[nodiscard]] constexpr bool operator==(const EnvironmentProjection&) const noexcept = default;
};
} // namespace epidemic::runtime
