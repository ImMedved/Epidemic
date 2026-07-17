#include "environment_runtime_impl.h"

#include "Epidemic/Runtime/Environment/climate_profile.h"
#include "Epidemic/Runtime/Environment/environment_projection.h"
#include "Epidemic/Runtime/Environment/environment_runtime.h"
#include "Epidemic/Runtime/Environment/environment_services.h"
#include "Epidemic/Runtime/Environment/environment_snapshot.h"
#include "Epidemic/Runtime/Environment/environment_update.h"
#include "Epidemic/Runtime/Environment/season_state.h"
#include "Epidemic/Runtime/Environment/surface_state.h"
#include "Epidemic/Runtime/Environment/weather_state.h"

#include <iostream>
#include <memory>
#include <type_traits>

namespace
{
using epidemic::foundation::Result;
using epidemic::runtime::ClimateProfile;
using epidemic::runtime::CreateEnvironmentServices;
using epidemic::runtime::EnvironmentProjection;
using epidemic::runtime::EnvironmentRuntime;
using epidemic::runtime::EnvironmentSnapshot;
using epidemic::runtime::EnvironmentStateUpdate;
using epidemic::runtime::EnvironmentUpdateInput;
using epidemic::runtime::GameDuration;
using epidemic::runtime::GameTimePoint;
using epidemic::runtime::IEnvironmentQuery;
using epidemic::runtime::IEnvironmentRuntime;
using epidemic::runtime::IEnvironmentUpdatePolicy;
using epidemic::runtime::IEnvironmentWriter;
using epidemic::runtime::RegionId;
using epidemic::runtime::SeasonKind;
using epidemic::runtime::SeasonState;
using epidemic::runtime::SurfaceConditionKind;
using epidemic::runtime::SurfaceId;
using epidemic::runtime::SurfaceState;
using epidemic::runtime::WeatherKind;
using epidemic::runtime::WeatherState;

[[nodiscard]] bool SeedRegion(EnvironmentRuntime& runtime, RegionId region)
{
    return runtime.SetWeather(region, WeatherState{WeatherKind::Rain, 0.7f, 0.8f, 0.6f, 4.0f, 180.0f}) &&
           runtime.SetSeason(region, SeasonState{SeasonKind::Autumn, 0.5f}) &&
           runtime.SetClimateProfile(region, ClimateProfile{9.0f, 0.75f, 3.0f, 900.0f});
}

[[nodiscard]] SurfaceState MakeSurface(SurfaceId surface, RegionId region)
{
    SurfaceState state{};
    state.surface_id = surface;
    state.region_id = region;
    state.condition = SurfaceConditionKind::Muddy;
    state.wetness = 0.6f;
    state.snow_depth = 0.2f;
    state.mud_depth = 0.4f;
    state.ice_thickness = 0.1f;
    state.temperature = 1.5f;
    return state;
}

class WetnessPolicy final : public IEnvironmentUpdatePolicy
{
  public:
    [[nodiscard]] Result<EnvironmentStateUpdate> BuildUpdate(const EnvironmentUpdateInput& input, const IEnvironmentQuery& query) const override
    {
        const auto surface = query.GetSurfaceState(SurfaceId{10});
        if (!surface)
        {
            return Result<EnvironmentStateUpdate>::Failure(surface.GetError());
        }

        SurfaceState updated = surface.Value();
        updated.wetness = 0.25f;
        updated.mud_depth = 0.7f;
        updated.condition = SurfaceConditionKind::Muddy;
        updated.temperature = static_cast<float>(input.game_delta.ticks) * 0.01f;

        EnvironmentStateUpdate update{};
        update.region = input.region_id;
        update.source_revision = query.GetRevision();
        update.surfaces.push_back(updated);
        return Result<EnvironmentStateUpdate>::Success(std::move(update));
    }
};

[[nodiscard]] bool TestUnknownRegionAndSurfaceFail()
{
    EnvironmentRuntime runtime;
    const auto weather = runtime.GetWeather(RegionId{77});
    const auto surface = runtime.GetSurfaceState(SurfaceId{88});
    const auto snapshot = runtime.BuildSnapshot(RegionId{77});
    return !weather && weather.GetError().HasCode("environment.region_unknown") && !surface &&
           surface.GetError().HasCode("environment.surface_unknown") && !snapshot && snapshot.GetError().HasCode("environment.region_unknown");
}

[[nodiscard]] bool TestSetGetWeatherSeasonClimateByRegion()
{
    EnvironmentRuntime runtime;
    const RegionId region{10};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }

    const auto weather = runtime.GetWeather(region);
    const auto season = runtime.GetSeason(region);
    const auto climate = runtime.GetClimateProfile(region);
    return weather && weather.Value().kind == WeatherKind::Rain && season && season.Value().kind == SeasonKind::Autumn && climate &&
           climate.Value().average_humidity == 0.75f && runtime.GetRevision() == 3u;
}

[[nodiscard]] bool TestSurfaceStateStoresRegionAndMixedConditions()
{
    EnvironmentRuntime runtime;
    const RegionId region{5};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }

    const auto set = runtime.SetSurfaceState(MakeSurface(SurfaceId{12}, region));
    const auto surface = runtime.GetSurfaceState(SurfaceId{12});
    return set && surface && surface.Value().region_id == region && surface.Value().condition == SurfaceConditionKind::Icy &&
           surface.Value().wetness == 0.6f &&
           surface.Value().snow_depth == 0.2f && surface.Value().mud_depth == 0.4f && surface.Value().ice_thickness == 0.1f &&
           surface.Value().revision == runtime.GetRevision();
}

[[nodiscard]] bool TestValidationRejectsInvalidValues()
{
    EnvironmentRuntime runtime;
    const auto invalid_weather = runtime.SetWeather(RegionId{1}, WeatherState{WeatherKind::Storm, 1.5f, 0.0f, 0.0f, 1.0f, 0.0f});
    const auto invalid_surface = runtime.SetSurfaceState(SurfaceState{SurfaceId{2}, RegionId{}, SurfaceConditionKind::Wet, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f});
    const auto invalid_update = runtime.Update(EnvironmentUpdateInput{GameTimePoint{100}, GameDuration{-1}, RegionId{1}});
    return !invalid_weather && invalid_weather.GetError().HasCode("environment.invalid_weather") && !invalid_surface &&
           invalid_surface.GetError().HasCode("environment.invalid_region") && !invalid_update &&
           invalid_update.GetError().HasCode("environment.invalid_delta");
}

[[nodiscard]] bool TestSnapshotAndProjectionAreRevisionedCopies()
{
    EnvironmentRuntime runtime;
    const RegionId region{8};
    if (!SeedRegion(runtime, region) || !runtime.SetSurfaceState(MakeSurface(SurfaceId{20}, region)) ||
        !runtime.SetSurfaceState(MakeSurface(SurfaceId{10}, region)))
    {
        return false;
    }

    const auto snapshot = runtime.BuildSnapshot(region);
    const auto projection = runtime.BuildProjection(region);
    if (!snapshot || !projection)
    {
        return false;
    }

    const auto changed_weather = runtime.SetWeather(region, WeatherState{WeatherKind::Clear, 0.0f, 0.1f, 0.0f, 1.0f, 0.0f});
    return changed_weather && snapshot.Value().region_id == region && snapshot.Value().revision < runtime.GetRevision() && snapshot.Value().surfaces.size() == 2u &&
           snapshot.Value().surfaces[0].surface_id == SurfaceId{10} && projection.Value().revision == snapshot.Value().revision &&
           projection.Value().weather.kind == WeatherKind::Rain;
}

[[nodiscard]] bool TestUpdateWithoutPolicyDoesNotMutateSurface()
{
    EnvironmentRuntime runtime;
    const RegionId region{3};
    if (!SeedRegion(runtime, region) || !runtime.SetSurfaceState(MakeSurface(SurfaceId{10}, region)))
    {
        return false;
    }

    const auto before = runtime.GetSurfaceState(SurfaceId{10});
    const auto update = runtime.Update(EnvironmentUpdateInput{GameTimePoint{1000}, GameDuration{100}, region});
    const auto after = runtime.GetSurfaceState(SurfaceId{10});
    return before && update && after && before.Value() == after.Value();
}

[[nodiscard]] bool TestOptionalPolicyCanMutateSurface()
{
    EnvironmentRuntime runtime;
    auto policy = std::make_shared<WetnessPolicy>();
    runtime.SetUpdatePolicy(policy);
    const RegionId region{3};
    if (!SeedRegion(runtime, region) || !runtime.SetSurfaceState(MakeSurface(SurfaceId{10}, region)))
    {
        return false;
    }

    const auto update = runtime.Update(EnvironmentUpdateInput{GameTimePoint{1000}, GameDuration{100}, region});
    const auto surface = runtime.GetSurfaceState(SurfaceId{10});
    return update && surface && surface.Value().wetness == 0.25f && surface.Value().mud_depth == 0.7f && surface.Value().temperature == 1.0f;
}

[[nodiscard]] bool TestApplyUpdateRejectsRevisionConflict()
{
    EnvironmentRuntime runtime;
    const RegionId region{44};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }

    EnvironmentStateUpdate update{};
    update.region = region;
    update.source_revision = runtime.GetRevision() + 1u;
    update.weather = WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 1.0f, 720.0f};

    const auto rejected = runtime.ApplyUpdate(update);
    const auto weather = runtime.GetWeather(region);
    return !rejected && rejected.GetError().HasCode("environment.revision_conflict") &&
           weather && weather.Value().kind == WeatherKind::Rain;
}

[[nodiscard]] bool TestBatchNormalizesWindDirection()
{
    EnvironmentRuntime runtime;
    const RegionId region{46};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }

    EnvironmentStateUpdate update{};
    update.region = region;
    update.source_revision = runtime.GetRevision();
    update.weather = WeatherState{WeatherKind::Cloudy, 0.2f, 0.5f, 0.0f, 3.0f, 725.0f};
    const auto applied = runtime.ApplyUpdate(update);
    const auto weather = runtime.GetWeather(region);
    return applied && weather && weather.Value().wind_direction_degrees == 5.0f;
}

[[nodiscard]] bool TestInvalidBatchLeavesStateUnchanged()
{
    EnvironmentRuntime runtime;
    const RegionId region{45};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }

    const auto before_weather = runtime.GetWeather(region);
    const auto before_revision = runtime.GetRevision();

    EnvironmentStateUpdate update{};
    update.region = region;
    update.source_revision = before_revision;
    update.weather = WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 2.0f, 90.0f};
    update.surfaces.push_back(SurfaceState{SurfaceId{99}, RegionId{}, SurfaceConditionKind::Wet, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f});

    const auto rejected = runtime.ApplyUpdate(update);
    const auto after_weather = runtime.GetWeather(region);
    return before_weather && !rejected && rejected.GetError().HasCode("environment.invalid_region") &&
           after_weather && after_weather.Value() == before_weather.Value() && runtime.GetRevision() == before_revision;
}

[[nodiscard]] bool TestFactoryCreatesSplitServices()
{
    auto policy = std::make_shared<WetnessPolicy>();
    const auto services = CreateEnvironmentServices({policy});
    if (!services || !services.Value().runtime || !services.Value().query || !services.Value().writer)
    {
        return false;
    }

    const RegionId region{22};
    if (!services.Value().writer->SetWeather(region, WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f}) ||
        !services.Value().writer->SetSeason(region, SeasonState{SeasonKind::Summer, 0.2f}) ||
        !services.Value().writer->SetClimateProfile(region, ClimateProfile{20.0f, 0.4f, 2.0f, 400.0f}))
    {
        return false;
    }

    const auto weather = services.Value().query->GetWeather(region);
    return weather && weather.Value().kind == WeatherKind::Clear;
}
} // namespace

int main()
{
    static_assert(std::is_trivially_copyable_v<WeatherState>);
    static_assert(std::is_trivially_copyable_v<SeasonState>);
    static_assert(std::is_trivially_copyable_v<ClimateProfile>);
    static_assert(std::is_trivially_copyable_v<SurfaceState>);
    static_assert(!std::is_trivially_copyable_v<EnvironmentSnapshot>);
    static_assert(std::is_trivially_copyable_v<EnvironmentProjection>);
    static_assert(std::is_trivially_copyable_v<EnvironmentUpdateInput>);
    static_assert(std::has_virtual_destructor_v<IEnvironmentRuntime>);
    static_assert(std::has_virtual_destructor_v<IEnvironmentQuery>);
    static_assert(std::has_virtual_destructor_v<IEnvironmentWriter>);

    struct NamedTest
    {
        const char* name;
        bool (*run)();
    };

    const NamedTest tests[] = {
        {"UnknownRegionAndSurfaceFail", TestUnknownRegionAndSurfaceFail},
        {"SetGetWeatherSeasonClimateByRegion", TestSetGetWeatherSeasonClimateByRegion},
        {"SurfaceStateStoresRegionAndMixedConditions", TestSurfaceStateStoresRegionAndMixedConditions},
        {"ValidationRejectsInvalidValues", TestValidationRejectsInvalidValues},
        {"SnapshotAndProjectionAreRevisionedCopies", TestSnapshotAndProjectionAreRevisionedCopies},
        {"UpdateWithoutPolicyDoesNotMutateSurface", TestUpdateWithoutPolicyDoesNotMutateSurface},
        {"OptionalPolicyCanMutateSurface", TestOptionalPolicyCanMutateSurface},
        {"ApplyUpdateRejectsRevisionConflict", TestApplyUpdateRejectsRevisionConflict},
        {"BatchNormalizesWindDirection", TestBatchNormalizesWindDirection},
        {"InvalidBatchLeavesStateUnchanged", TestInvalidBatchLeavesStateUnchanged},
        {"FactoryCreatesSplitServices", TestFactoryCreatesSplitServices},
    };

    for (const NamedTest& test : tests)
    {
        if (!test.run())
        {
            std::cerr << "Environment test failed: " << test.name << "\n";
            return 1;
        }
    }

    return 0;
}

