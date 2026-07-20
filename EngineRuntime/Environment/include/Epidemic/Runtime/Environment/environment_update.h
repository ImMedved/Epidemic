#pragma once

#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/season_state.h"
#include "Epidemic/Runtime/Environment/surface_state.h"
#include "Epidemic/Runtime/Environment/weather_state.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace epidemic::runtime
{
struct EnvironmentUpdateInput
{
    GameTimePoint game_time{};
    GameDuration game_delta{};
    RegionId region_id{};

    [[nodiscard]] constexpr bool operator==(const EnvironmentUpdateInput&) const noexcept = default;
};

struct EnvironmentStateUpdate
{
    RegionId region{};
    std::uint64_t source_revision = 0;
    std::optional<WeatherState> weather;
    std::optional<SeasonState> season;
    std::optional<ClimateProfile> climate;
    std::vector<SurfaceState> surfaces;
};
} // namespace epidemic::runtime
