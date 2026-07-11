#include "environment_runtime_impl.h"

#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/environment_projection.h"
#include "Epidemic/Runtime/Environment/environment_runtime.h"
#include "Epidemic/Runtime/Environment/environment_snapshot.h"
#include "Epidemic/Runtime/Environment/environment_update.h"
#include "Epidemic/Runtime/Environment/season_state.h"
#include "Epidemic/Runtime/Environment/surface_state.h"
#include "Epidemic/Runtime/Environment/weather_state.h"

#include <type_traits>

namespace
{
using epidemic::runtime::ClimateProfile;
using epidemic::runtime::EnvironmentProjection;
using epidemic::runtime::EnvironmentRuntime;
using epidemic::runtime::EnvironmentSnapshot;
using epidemic::runtime::EnvironmentUpdateInput;
using epidemic::runtime::IEnvironmentRuntime;
using epidemic::runtime::RegionId;
using epidemic::runtime::SeasonKind;
using epidemic::runtime::SeasonState;
using epidemic::runtime::SurfaceConditionKind;
using epidemic::runtime::SurfaceId;
using epidemic::runtime::SurfaceState;
using epidemic::runtime::WeatherKind;
using epidemic::runtime::WeatherState;

bool TestDefaultWeatherIsClear()
{
    EnvironmentRuntime runtime;
    const WeatherState weather = runtime.GetWeather(RegionId{77});
    return weather.kind == WeatherKind::Clear && weather.intensity == 0.0f;
}

bool TestSetGetWeatherByRegion()
{
    EnvironmentRuntime runtime;
    const RegionId region{10};
    runtime.SetWeather(region, WeatherState{WeatherKind::Storm, 1.0f, 1.0f, 0.9f, 12.0f, 270.0f});

    const WeatherState weather = runtime.GetWeather(region);
    return weather.kind == WeatherKind::Storm && weather.wind_speed == 12.0f && weather.precipitation == 0.9f;
}

bool TestSetGetSeasonByRegion()
{
    EnvironmentRuntime runtime;
    const RegionId region{11};
    runtime.SetSeason(region, SeasonState{SeasonKind::Winter, 0.4f});

    const SeasonState season = runtime.GetSeason(region);
    return season.kind == SeasonKind::Winter && season.progress == 0.4f;
}

bool TestSetGetSurfaceState()
{
    EnvironmentRuntime runtime;
    runtime.SetSurfaceState(SurfaceState{SurfaceId{12}, SurfaceConditionKind::Muddy, 0.2f, 0.0f, 0.4f, 0.0f, 8.0f});

    const auto surface = runtime.GetSurfaceState(SurfaceId{12});
    return surface.has_value() && surface->condition == SurfaceConditionKind::Muddy && surface->mud_depth == 0.4f;
}

bool TestSnapshotStoresClimateAndRegion()
{
    EnvironmentRuntime runtime;
    runtime.SetClimateProfile(RegionId{5}, ClimateProfile{7.0f, 0.65f, 3.5f, 1100.0f});
    const EnvironmentSnapshot snapshot = runtime.BuildSnapshot(RegionId{5});

    return snapshot.region_id.IsValid() && snapshot.temperature == 7.0f && snapshot.humidity == 0.65f &&
           snapshot.climate.average_wind_speed == 3.5f;
}

bool TestProjectionIsSnapshotCopy()
{
    EnvironmentRuntime runtime;
    const RegionId region{8};
    runtime.SetWeather(region, WeatherState{WeatherKind::Rain, 0.8f, 0.9f, 0.7f, 4.0f, 180.0f});
    runtime.SetSeason(region, SeasonState{SeasonKind::Autumn, 0.6f});
    runtime.SetClimateProfile(region, ClimateProfile{9.5f, 0.8f, 5.0f, 1300.0f});

    const EnvironmentProjection projection = runtime.BuildProjection(region);
    runtime.SetWeather(region, WeatherState{WeatherKind::Clear, 0.0f, 0.1f, 0.0f, 1.0f, 0.0f});

    return projection.region_id == region && projection.weather.kind == WeatherKind::Rain &&
           projection.season.kind == SeasonKind::Autumn && projection.temperature == 9.5f && projection.humidity == 0.8f;
}

bool TestUpdateCanDryWetSurface()
{
    EnvironmentRuntime runtime;
    runtime.SetSurfaceState(SurfaceState{SurfaceId{11}, SurfaceConditionKind::Wet, 0.6f, 0.0f, 0.0f, 0.0f, 12.0f});

    const auto result = runtime.Update(EnvironmentUpdateInput{1000u, 1000, RegionId{3}});
    const auto surface = runtime.GetSurfaceState(SurfaceId{11});

    return result.HasValue() && surface.has_value() && surface->wetness < 0.6f &&
           surface->condition == SurfaceConditionKind::Drying;
}
} // namespace

int main()
{
    static_assert(std::is_trivially_copyable_v<WeatherState>);
    static_assert(std::is_trivially_copyable_v<SeasonState>);
    static_assert(std::is_trivially_copyable_v<ClimateProfile>);
    static_assert(std::is_trivially_copyable_v<SurfaceState>);
    static_assert(std::is_trivially_copyable_v<EnvironmentSnapshot>);
    static_assert(std::is_trivially_copyable_v<EnvironmentProjection>);
    static_assert(std::is_trivially_copyable_v<EnvironmentUpdateInput>);
    static_assert(std::has_virtual_destructor_v<IEnvironmentRuntime>);

    if (!TestDefaultWeatherIsClear())
    {
        return 1;
    }

    if (!TestSetGetWeatherByRegion())
    {
        return 2;
    }

    if (!TestSetGetSeasonByRegion())
    {
        return 3;
    }

    if (!TestSetGetSurfaceState())
    {
        return 4;
    }

    if (!TestSnapshotStoresClimateAndRegion())
    {
        return 5;
    }

    if (!TestProjectionIsSnapshotCopy())
    {
        return 6;
    }

    if (!TestUpdateCanDryWetSurface())
    {
        return 7;
    }

    return 0;
}
