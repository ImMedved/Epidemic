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
#include <limits>
#include <stdexcept>
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
    return static_cast<bool>(runtime.RegisterRegionEnvironment(region,
                                                               WeatherState{WeatherKind::Rain, 0.7f, 0.8f, 0.6f, 4.0f, 180.0f, 11.0f, 0.65f},
                                                               SeasonState{SeasonKind::Autumn, 0.5f},
                                                               ClimateProfile{9.0f, 0.75f, 3.0f, 900.0f}));
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
        const auto region_revision = query.GetRegionRevision(input.region_id);
        if (!region_revision)
        {
            return Result<EnvironmentStateUpdate>::Failure(region_revision.GetError());
        }
        update.source_revision = region_revision.Value();
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
    const auto region_revision = runtime.GetRegionRevision(region);
    return weather && weather.Value().kind == WeatherKind::Rain && season && season.Value().kind == SeasonKind::Autumn && climate &&
           climate.Value().average_humidity == 0.75f && region_revision && region_revision.Value() == 1u && runtime.GetRevision() == 1u;
}

[[nodiscard]] bool TestDuplicateRegionRegistrationRejected()
{
    EnvironmentRuntime runtime;
    const RegionId region{12};
    const auto first = runtime.RegisterRegionEnvironment(region,
                                                          WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
                                                          SeasonState{SeasonKind::Spring, 0.0f},
                                                          ClimateProfile{10.0f, 0.5f, 1.0f, 300.0f});
    const auto duplicate = runtime.RegisterRegionEnvironment(region,
                                                              WeatherState{WeatherKind::Storm, 1.0f, 1.0f, 1.0f, 8.0f, 90.0f},
                                                              SeasonState{SeasonKind::Winter, 0.8f},
                                                              ClimateProfile{-5.0f, 0.8f, 4.0f, 800.0f});
    const auto weather = runtime.GetWeather(region);
    return first && !duplicate && duplicate.GetError().HasCode("environment.region_already_registered") &&
           weather && weather.Value().kind == WeatherKind::Clear && runtime.GetRevision() == 1u;
}

[[nodiscard]] bool TestNoOpSettersDoNotBumpRevision()
{
    EnvironmentRuntime runtime;
    const RegionId region{13};
    if (!SeedRegion(runtime, region) || !runtime.SetSurfaceState(MakeSurface(SurfaceId{13}, region)))
    {
        return false;
    }

    const auto weather = runtime.GetWeather(region);
    const auto season = runtime.GetSeason(region);
    const auto climate = runtime.GetClimateProfile(region);
    const auto surface = runtime.GetSurfaceState(SurfaceId{13});
    const auto before_global = runtime.GetRevision();
    const auto before_region = runtime.GetRegionRevision(region);
    if (!weather || !season || !climate || !surface || !before_region)
    {
        return false;
    }

    SurfaceState same_surface = surface.Value();
    same_surface.revision = 0;
    const auto set_weather = runtime.SetWeather(region, weather.Value());
    const auto set_season = runtime.SetSeason(region, season.Value());
    const auto set_climate = runtime.SetClimateProfile(region, climate.Value());
    const auto set_surface = runtime.SetSurfaceState(same_surface);
    const auto after_region = runtime.GetRegionRevision(region);
    return set_weather && set_season && set_climate && set_surface &&
           after_region && runtime.GetRevision() == before_global && after_region.Value() == before_region.Value();
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
    const auto region_revision = runtime.GetRegionRevision(region);
    return set && surface && surface.Value().region_id == region && surface.Value().condition == SurfaceConditionKind::Icy &&
           surface.Value().wetness == 0.6f &&
           surface.Value().snow_depth == 0.2f && surface.Value().mud_depth == 0.4f && surface.Value().ice_thickness == 0.1f &&
           region_revision && surface.Value().revision == region_revision.Value();
}

[[nodiscard]] bool TestSurfaceRegionOwnershipIsImmutable()
{
    EnvironmentRuntime runtime;
    const RegionId first{13};
    const RegionId second{14};
    const SurfaceId surface{10};
    if (!SeedRegion(runtime, first) || !SeedRegion(runtime, second) || !runtime.SetSurfaceState(MakeSurface(surface, first)))
    {
        return false;
    }

    const auto before_first = runtime.GetRegionRevision(first);
    const auto before_second = runtime.GetRegionRevision(second);
    SurfaceState moved = MakeSurface(surface, second);
    moved.wetness = 0.9f;
    const auto rejected = runtime.SetSurfaceState(moved);
    const auto after_first = runtime.GetRegionRevision(first);
    const auto after_second = runtime.GetRegionRevision(second);
    const auto stored = runtime.GetSurfaceState(surface);
    return before_first && before_second && after_first && after_second && stored &&
           !rejected && rejected.GetError().HasCode("environment.surface_region_mismatch") &&
           after_first.Value() == before_first.Value() && after_second.Value() == before_second.Value() &&
           stored.Value().region_id == first;
}

[[nodiscard]] bool TestValidationRejectsInvalidValues()
{
    EnvironmentRuntime runtime;
    const auto invalid_weather = runtime.RegisterRegionEnvironment(RegionId{1},
                                                                   WeatherState{WeatherKind::Storm, 1.5f, 0.0f, 0.0f, 1.0f, 0.0f},
                                                                   SeasonState{SeasonKind::Spring, 0.0f},
                                                                   ClimateProfile{0.0f, 0.5f, 0.0f, 0.0f});
    const auto invalid_surface = runtime.SetSurfaceState(SurfaceState{SurfaceId{2}, RegionId{}, SurfaceConditionKind::Wet, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f});
    const auto invalid_update = runtime.Update(EnvironmentUpdateInput{GameTimePoint{100}, GameDuration{-1}, RegionId{1}});
    return !invalid_weather && invalid_weather.GetError().HasCode("environment.invalid_weather") && !invalid_surface &&
           invalid_surface.GetError().HasCode("environment.invalid_region") && !invalid_update &&
           invalid_update.GetError().HasCode("environment.invalid_delta");
}

[[nodiscard]] bool TestPartialRegionInitializationRejected()
{
    EnvironmentRuntime runtime;
    const RegionId region{31};

    const auto set_weather = runtime.SetWeather(region, WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f});
    const auto surface = runtime.SetSurfaceState(MakeSurface(SurfaceId{31}, region));
    const auto registered = runtime.RegisterRegionEnvironment(region,
                                                              WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f},
                                                              SeasonState{SeasonKind::Summer, 0.2f},
                                                              ClimateProfile{20.0f, 0.4f, 2.0f, 400.0f});
    const auto set_weather_after = runtime.SetWeather(region, WeatherState{WeatherKind::Cloudy, 0.1f, 0.5f, 0.0f, 2.0f, 0.0f});

    return !set_weather && set_weather.GetError().HasCode("environment.region_unknown") && !surface &&
           surface.GetError().HasCode("environment.region_unknown") && registered && set_weather_after;
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

    const auto changed_weather = runtime.SetWeather(region, WeatherState{WeatherKind::Clear, 0.0f, 0.1f, 0.0f, 1.0f, 0.0f, 18.0f, 0.2f});
    return changed_weather && snapshot.Value().region_id == region && snapshot.Value().revision < runtime.GetRevision() && snapshot.Value().surfaces.size() == 2u &&
           snapshot.Value().surfaces[0].surface_id == SurfaceId{10} && projection.Value().revision == snapshot.Value().revision &&
           projection.Value().weather.kind == WeatherKind::Rain && projection.Value().temperature == 11.0f &&
           projection.Value().humidity == 0.65f;
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
    const auto region_revision = runtime.GetRegionRevision(region);
    if (!region_revision)
    {
        return false;
    }
    update.source_revision = region_revision.Value() + 1u;
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
    const auto region_revision = runtime.GetRegionRevision(region);
    if (!region_revision)
    {
        return false;
    }
    update.source_revision = region_revision.Value();
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
    const auto before_region_revision = runtime.GetRegionRevision(region);
    if (!before_region_revision)
    {
        return false;
    }

    EnvironmentStateUpdate update{};
    update.region = region;
    update.source_revision = before_region_revision.Value();
    update.weather = WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 2.0f, 90.0f};
    update.surfaces.push_back(SurfaceState{SurfaceId{99}, RegionId{}, SurfaceConditionKind::Wet, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f});

    const auto rejected = runtime.ApplyUpdate(update);
    const auto after_weather = runtime.GetWeather(region);
    return before_weather && !rejected && rejected.GetError().HasCode("environment.invalid_region") &&
           after_weather && after_weather.Value() == before_weather.Value() && runtime.GetRevision() == before_revision;
}

[[nodiscard]] bool TestEmptyUpdateDoesNotBumpRevision()
{
    EnvironmentRuntime runtime;
    const RegionId region{47};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }

    const auto before_global = runtime.GetRevision();
    const auto before_region = runtime.GetRegionRevision(region);
    if (!before_region)
    {
        return false;
    }

    EnvironmentStateUpdate update{};
    update.region = region;
    update.source_revision = before_region.Value();
    const auto applied = runtime.ApplyUpdate(update);
    const auto after_region = runtime.GetRegionRevision(region);
    return applied && after_region && runtime.GetRevision() == before_global && after_region.Value() == before_region.Value();
}

[[nodiscard]] bool TestCrossRegionRevisionDoesNotConflict()
{
    EnvironmentRuntime runtime;
    const RegionId first{50};
    const RegionId second{51};
    if (!SeedRegion(runtime, first) || !SeedRegion(runtime, second))
    {
        return false;
    }

    const auto first_revision = runtime.GetRegionRevision(first);
    if (!first_revision || !runtime.SetWeather(second, WeatherState{WeatherKind::Cloudy, 0.2f, 0.5f, 0.0f, 3.0f, 90.0f}))
    {
        return false;
    }

    EnvironmentStateUpdate update{};
    update.region = first;
    update.source_revision = first_revision.Value();
    update.weather = WeatherState{WeatherKind::Fog, 0.1f, 0.7f, 0.0f, 1.0f, 15.0f};
    const auto applied = runtime.ApplyUpdate(update);
    const auto first_weather = runtime.GetWeather(first);
    return applied && first_weather && first_weather.Value().kind == WeatherKind::Fog;
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
    if (!services.Value().writer->RegisterRegionEnvironment(region,
                                                            WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f},
                                                            SeasonState{SeasonKind::Summer, 0.2f},
                                                            ClimateProfile{20.0f, 0.4f, 2.0f, 400.0f}))
    {
        return false;
    }

    const auto weather = services.Value().query->GetWeather(region);
    return weather && weather.Value().kind == WeatherKind::Clear;
}

class ThrowingPolicy final : public IEnvironmentUpdatePolicy
{
public:
    epidemic::foundation::Result<EnvironmentStateUpdate> BuildUpdate(
        const EnvironmentUpdateInput&,
        const IEnvironmentQuery&) const override
    {
        throw std::runtime_error("policy failure");
    }
};

[[nodiscard]] bool TestBatchOwnershipAndDuplicatesAreTransactional()
{
    EnvironmentRuntime runtime;
    const RegionId first{70};
    const RegionId second{71};
    const SurfaceId surface{700};
    if (!SeedRegion(runtime, first) || !SeedRegion(runtime, second) ||
        !runtime.SetSurfaceState(SurfaceState{surface, first, SurfaceConditionKind::Dry, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}))
    {
        return false;
    }
    const auto before_global = runtime.GetRevision();
    const auto before_first = runtime.GetRegionRevision(first);
    const auto before_second = runtime.GetRegionRevision(second);
    const auto before_surface = runtime.GetSurfaceState(surface);
    if (!before_first || !before_second || !before_surface)
    {
        return false;
    }

    EnvironmentStateUpdate moved{};
    moved.region = second;
    moved.source_revision = before_second.Value();
    moved.surfaces.push_back(SurfaceState{surface, second, SurfaceConditionKind::Wet, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f});
    const auto rejected_move = runtime.ApplyUpdate(moved);
    if (rejected_move || !rejected_move.GetError().HasCode("environment.surface_region_mismatch") ||
        runtime.GetRevision() != before_global || runtime.GetRegionRevision(first).Value() != before_first.Value() ||
        runtime.GetRegionRevision(second).Value() != before_second.Value() || runtime.GetSurfaceState(surface).Value() != before_surface.Value())
    {
        return false;
    }

    EnvironmentStateUpdate duplicate{};
    duplicate.region = first;
    duplicate.source_revision = before_first.Value();
    duplicate.surfaces.push_back(SurfaceState{SurfaceId{701}, first, SurfaceConditionKind::Wet, 0.2f, 0.0f, 0.0f, 0.0f, 1.0f});
    duplicate.surfaces.push_back(SurfaceState{SurfaceId{701}, first, SurfaceConditionKind::Wet, 0.4f, 0.0f, 0.0f, 0.0f, 1.0f});
    const auto rejected_duplicate = runtime.ApplyUpdate(duplicate);
    return !rejected_duplicate && rejected_duplicate.GetError().HasCode("environment.duplicate_surface_update") &&
           runtime.GetRevision() == before_global && !runtime.GetSurfaceState(SurfaceId{701});
}

[[nodiscard]] bool TestEnvironmentEnumRevisionAllocationAndFreezeContracts()
{
    EnvironmentRuntime runtime;
    const RegionId region{72};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }
    const auto before = runtime.BuildSnapshot(region);
    const auto before_global = runtime.GetRevision();
    if (!before)
    {
        return false;
    }
    WeatherState invalid_weather = before.Value().weather;
    invalid_weather.kind = static_cast<WeatherKind>(255);
    SeasonState invalid_season = before.Value().season;
    invalid_season.kind = static_cast<SeasonKind>(255);
    const auto weather_failed = runtime.SetWeather(region, invalid_weather);
    const auto season_failed = runtime.SetSeason(region, invalid_season);
    if (weather_failed || season_failed || runtime.GetRevision() != before_global)
    {
        return false;
    }

    runtime.SetRevisionForTesting(std::numeric_limits<std::uint64_t>::max());
    const auto overflow_before = runtime.BuildSnapshot(region);
    const auto overflow = runtime.SetWeather(region, WeatherState{WeatherKind::Cloudy, 0.2f, 0.4f, 0.0f, 1.0f, 90.0f});
    if (!overflow_before || overflow || !overflow.GetError().HasCode("environment.revision_overflow") ||
        runtime.BuildSnapshot(region).Value().weather != overflow_before.Value().weather)
    {
        return false;
    }

    EnvironmentRuntime allocation;
    allocation.FailNextAllocationForTesting();
    const auto failed_registration = allocation.RegisterRegionEnvironment(
        RegionId{80}, WeatherState{}, SeasonState{}, ClimateProfile{10.0f, 0.3f, 1.0f, 100.0f});
    if (failed_registration || allocation.GetRevision() != 0 || allocation.GetWeather(RegionId{80}))
    {
        return false;
    }
    if (!SeedRegion(allocation, RegionId{81}))
    {
        return false;
    }
    allocation.FreezeRegistration();
    const auto frozen_revision = allocation.GetRevision();
    const auto frozen = allocation.RegisterRegionEnvironment(
        RegionId{82}, WeatherState{}, SeasonState{}, ClimateProfile{10.0f, 0.3f, 1.0f, 100.0f});
    return allocation.IsRegistrationFrozen() && !frozen && frozen.GetError().HasCode("environment.registration_frozen") &&
           allocation.GetRevision() == frozen_revision;
}

[[nodiscard]] bool TestFreezeRejectsNewSurfacesButAllowsExistingUpdates()
{
    EnvironmentRuntime runtime;
    const RegionId region{83};
    const SurfaceId existing_surface{830};
    const SurfaceId new_surface{831};
    if (!SeedRegion(runtime, region) || !runtime.SetSurfaceState(MakeSurface(existing_surface, region)))
    {
        return false;
    }

    runtime.FreezeRegistration();
    runtime.FreezeRegistration();
    const auto before_global = runtime.GetRevision();
    const auto before_region = runtime.GetRegionRevision(region);
    if (!before_region)
    {
        return false;
    }

    const auto rejected_direct = runtime.SetSurfaceState(MakeSurface(new_surface, region));
    if (rejected_direct || !rejected_direct.GetError().HasCode("environment.registration_frozen") ||
        runtime.GetRevision() != before_global || runtime.GetSurfaceState(new_surface))
    {
        return false;
    }

    EnvironmentStateUpdate batch{};
    batch.region = region;
    batch.source_revision = before_region.Value();
    batch.surfaces.push_back(MakeSurface(new_surface, region));
    const auto rejected_batch = runtime.ApplyUpdate(batch);
    if (rejected_batch || !rejected_batch.GetError().HasCode("environment.registration_frozen") ||
        runtime.GetRevision() != before_global || runtime.GetSurfaceState(new_surface))
    {
        return false;
    }

    SurfaceState changed = MakeSurface(existing_surface, region);
    changed.wetness = 0.3f;
    changed.snow_depth = 0.0f;
    changed.mud_depth = 0.0f;
    changed.ice_thickness = 0.0f;
    const auto updated_existing = runtime.SetSurfaceState(changed);
    const auto stored = runtime.GetSurfaceState(existing_surface);
    if (!updated_existing || !stored || stored.Value().wetness != 0.3f || !runtime.IsRegistrationFrozen())
    {
        return false;
    }

    const auto current_revision = runtime.GetRegionRevision(region);
    if (!current_revision)
    {
        return false;
    }
    EnvironmentStateUpdate existing_batch{};
    existing_batch.region = region;
    existing_batch.source_revision = current_revision.Value();
    SurfaceState batch_changed = stored.Value();
    batch_changed.temperature = 4.0f;
    existing_batch.surfaces.push_back(batch_changed);
    const auto updated_batch = runtime.ApplyUpdate(existing_batch);
    const auto final_surface = runtime.GetSurfaceState(existing_surface);
    return updated_batch && final_surface && final_surface.Value().temperature == 4.0f;
}

class ResultFailurePolicy final : public IEnvironmentUpdatePolicy
{
  public:
    [[nodiscard]] Result<EnvironmentStateUpdate> BuildUpdate(
        const EnvironmentUpdateInput&,
        const IEnvironmentQuery& query) const override
    {
        const auto missing = query.GetRegionRevision(RegionId{999999});
        if (missing)
        {
            return Result<EnvironmentStateUpdate>::Success(EnvironmentStateUpdate{});
        }
        return Result<EnvironmentStateUpdate>::Failure(missing.GetError());
    }
};

class InvalidOutputPolicy final : public IEnvironmentUpdatePolicy
{
  public:
    enum class Mode
    {
        InvalidWeather,
        WrongSurfaceRegion,
    };

    explicit InvalidOutputPolicy(Mode mode) : mode_(mode)
    {
    }

    [[nodiscard]] Result<EnvironmentStateUpdate> BuildUpdate(
        const EnvironmentUpdateInput& input,
        const IEnvironmentQuery& query) const override
    {
        const auto revision = query.GetRegionRevision(input.region_id);
        if (!revision)
        {
            return Result<EnvironmentStateUpdate>::Failure(revision.GetError());
        }

        EnvironmentStateUpdate update{};
        update.region = input.region_id;
        update.source_revision = revision.Value();
        if (mode_ == Mode::InvalidWeather)
        {
            WeatherState weather{};
            weather.kind = static_cast<WeatherKind>(255);
            update.weather = weather;
        }
        else
        {
            update.surfaces.push_back(SurfaceState{
                SurfaceId{990}, RegionId{991}, SurfaceConditionKind::Wet, 0.5f, 0.0f, 0.0f, 0.0f, 2.0f});
        }
        return Result<EnvironmentStateUpdate>::Success(std::move(update));
    }

  private:
    Mode mode_;
};

[[nodiscard]] bool TestRegistrationValidationAndWindNormalization()
{
    EnvironmentRuntime runtime;
    const RegionId region{84};

    WeatherState invalid_weather{};
    invalid_weather.kind = static_cast<WeatherKind>(255);
    SeasonState invalid_season{};
    invalid_season.kind = static_cast<SeasonKind>(255);
    ClimateProfile invalid_climate{};
    invalid_climate.average_humidity = std::numeric_limits<float>::quiet_NaN();

    const auto bad_region = runtime.RegisterRegionEnvironment(RegionId{}, WeatherState{}, SeasonState{}, ClimateProfile{});
    const auto bad_weather = runtime.RegisterRegionEnvironment(region, invalid_weather, SeasonState{}, ClimateProfile{});
    const auto bad_season = runtime.RegisterRegionEnvironment(region, WeatherState{}, invalid_season, ClimateProfile{});
    const auto bad_climate = runtime.RegisterRegionEnvironment(region, WeatherState{}, SeasonState{}, invalid_climate);
    if (bad_region || bad_weather || bad_season || bad_climate || runtime.GetRevision() != 0u || runtime.GetWeather(region))
    {
        return false;
    }

    WeatherState weather{};
    weather.wind_direction_degrees = -725.0f;
    const auto registered = runtime.RegisterRegionEnvironment(
        region, weather, SeasonState{SeasonKind::Spring, 0.0f}, ClimateProfile{10.0f, 0.5f, 1.0f, 100.0f});
    const auto stored = runtime.GetWeather(region);
    return registered && stored && stored.Value().wind_direction_degrees == 355.0f && runtime.GetRevision() == 1u;
}

[[nodiscard]] bool TestFiniteNumericValidationIsTransactional()
{
    EnvironmentRuntime runtime;
    const RegionId region{85};
    const SurfaceId surface{850};
    if (!SeedRegion(runtime, region) || !runtime.SetSurfaceState(MakeSurface(surface, region)))
    {
        return false;
    }

    const auto before = runtime.BuildSnapshot(region);
    const auto before_global = runtime.GetRevision();
    const auto before_region = runtime.GetRegionRevision(region);
    if (!before || !before_region)
    {
        return false;
    }

    WeatherState bad_weather = before.Value().weather;
    bad_weather.current_temperature = std::numeric_limits<float>::quiet_NaN();
    SeasonState bad_season = before.Value().season;
    bad_season.progress = std::numeric_limits<float>::infinity();
    ClimateProfile bad_climate = before.Value().climate;
    bad_climate.average_wind_speed = -1.0f;
    SurfaceState bad_surface = before.Value().surfaces.front();
    bad_surface.wetness = std::numeric_limits<float>::quiet_NaN();

    const auto weather_failed = runtime.SetWeather(region, bad_weather);
    const auto season_failed = runtime.SetSeason(region, bad_season);
    const auto climate_failed = runtime.SetClimateProfile(region, bad_climate);
    const auto surface_failed = runtime.SetSurfaceState(bad_surface);
    const auto after = runtime.BuildSnapshot(region);
    const auto after_region = runtime.GetRegionRevision(region);
    return !weather_failed && weather_failed.GetError().HasCode("environment.invalid_weather") &&
           !season_failed && season_failed.GetError().HasCode("environment.invalid_season") &&
           !climate_failed && climate_failed.GetError().HasCode("environment.invalid_climate") &&
           !surface_failed && surface_failed.GetError().HasCode("environment.invalid_surface_state") &&
           after && after.Value() == before.Value() && after_region && after_region.Value() == before_region.Value() &&
           runtime.GetRevision() == before_global;
}

[[nodiscard]] bool TestAllocationFailuresPreserveStateAndRetry()
{
    EnvironmentRuntime runtime;
    const RegionId region{86};
    const SurfaceId first_surface{860};
    const SurfaceId second_surface{861};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }

    const auto before_direct_revision = runtime.GetRevision();
    runtime.FailNextAllocationForTesting();
    const auto failed_direct = runtime.SetSurfaceState(MakeSurface(first_surface, region));
    if (failed_direct || !failed_direct.GetError().HasCode("environment.allocation_failed") ||
        runtime.GetRevision() != before_direct_revision || runtime.GetSurfaceState(first_surface))
    {
        return false;
    }
    if (!runtime.SetSurfaceState(MakeSurface(first_surface, region)))
    {
        return false;
    }

    const auto before_batch = runtime.BuildSnapshot(region);
    const auto before_region = runtime.GetRegionRevision(region);
    const auto before_batch_global = runtime.GetRevision();
    if (!before_batch || !before_region)
    {
        return false;
    }

    EnvironmentStateUpdate update{};
    update.region = region;
    update.source_revision = before_region.Value();
    update.weather = WeatherState{WeatherKind::Cloudy, 0.2f, 0.5f, 0.0f, 2.0f, 45.0f, 12.0f, 0.4f};
    update.surfaces.push_back(MakeSurface(second_surface, region));

    runtime.FailNextAllocationForTesting();
    const auto failed_batch = runtime.ApplyUpdate(update);
    const auto after_failed_batch = runtime.BuildSnapshot(region);
    if (failed_batch || !failed_batch.GetError().HasCode("environment.allocation_failed") ||
        !after_failed_batch || after_failed_batch.Value() != before_batch.Value() ||
        runtime.GetRevision() != before_batch_global || runtime.GetSurfaceState(second_surface))
    {
        return false;
    }

    const auto retry = runtime.ApplyUpdate(update);
    const auto weather = runtime.GetWeather(region);
    const auto surface = runtime.GetSurfaceState(second_surface);
    return retry && weather && weather.Value().kind == WeatherKind::Cloudy && surface;
}

[[nodiscard]] bool TestRevisionExhaustionPreservesBatchStateAndNoOpStillSucceeds()
{
    EnvironmentRuntime runtime;
    const RegionId region{87};
    const SurfaceId surface{870};
    if (!SeedRegion(runtime, region) || !runtime.SetSurfaceState(MakeSurface(surface, region)))
    {
        return false;
    }

    const auto before = runtime.BuildSnapshot(region);
    const auto region_revision = runtime.GetRegionRevision(region);
    if (!before || !region_revision)
    {
        return false;
    }
    runtime.SetRevisionForTesting(std::numeric_limits<std::uint64_t>::max());

    EnvironmentStateUpdate changed{};
    changed.region = region;
    changed.source_revision = region_revision.Value();
    changed.weather = WeatherState{WeatherKind::Clear, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 20.0f, 0.2f};
    SurfaceState changed_surface = before.Value().surfaces.front();
    changed_surface.wetness = 0.1f;
    changed_surface.snow_depth = 0.0f;
    changed_surface.mud_depth = 0.0f;
    changed_surface.ice_thickness = 0.0f;
    changed.surfaces.push_back(changed_surface);
    const auto overflow = runtime.ApplyUpdate(changed);
    const auto after_overflow = runtime.BuildSnapshot(region);
    if (overflow || !overflow.GetError().HasCode("environment.revision_overflow") || !after_overflow ||
        after_overflow.Value() != before.Value() || runtime.GetRevision() != std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }

    EnvironmentStateUpdate no_op{};
    no_op.region = region;
    no_op.source_revision = region_revision.Value();
    no_op.weather = before.Value().weather;
    no_op.season = before.Value().season;
    no_op.climate = before.Value().climate;
    no_op.surfaces = before.Value().surfaces;
    const auto accepted_no_op = runtime.ApplyUpdate(no_op);
    return accepted_no_op && runtime.BuildSnapshot(region).Value() == before.Value() &&
           runtime.GetRevision() == std::numeric_limits<std::uint64_t>::max();
}

[[nodiscard]] bool TestPolicyResultAndInvalidOutputFailuresPreserveState()
{
    EnvironmentRuntime runtime;
    const RegionId region{88};
    if (!SeedRegion(runtime, region) || !runtime.SetSurfaceState(MakeSurface(SurfaceId{880}, region)))
    {
        return false;
    }
    const auto before = runtime.BuildSnapshot(region);
    const auto before_global = runtime.GetRevision();
    if (!before)
    {
        return false;
    }

    runtime.SetUpdatePolicy(std::make_shared<ResultFailurePolicy>());
    const auto result_failure = runtime.Update(EnvironmentUpdateInput{.game_delta = GameDuration{1}, .region_id = region});
    if (result_failure || !result_failure.GetError().HasCode("environment.region_unknown") ||
        runtime.GetRevision() != before_global || runtime.BuildSnapshot(region).Value() != before.Value())
    {
        return false;
    }

    runtime.SetUpdatePolicy(std::make_shared<InvalidOutputPolicy>(InvalidOutputPolicy::Mode::InvalidWeather));
    const auto invalid_weather = runtime.Update(EnvironmentUpdateInput{.game_delta = GameDuration{1}, .region_id = region});
    if (invalid_weather || !invalid_weather.GetError().HasCode("environment.invalid_weather_kind") ||
        runtime.GetRevision() != before_global || runtime.BuildSnapshot(region).Value() != before.Value())
    {
        return false;
    }

    runtime.SetUpdatePolicy(std::make_shared<InvalidOutputPolicy>(InvalidOutputPolicy::Mode::WrongSurfaceRegion));
    const auto invalid_ownership = runtime.Update(EnvironmentUpdateInput{.game_delta = GameDuration{1}, .region_id = region});
    return !invalid_ownership && invalid_ownership.GetError().HasCode("environment.invalid_region") &&
           runtime.GetRevision() == before_global && runtime.BuildSnapshot(region).Value() == before.Value();
}

[[nodiscard]] bool TestEnvironmentPolicyExceptionAndNoOpBatch()
{
    EnvironmentRuntime runtime;
    const RegionId region{73};
    if (!SeedRegion(runtime, region))
    {
        return false;
    }
    const auto before = runtime.BuildSnapshot(region);
    const auto before_region_revision = runtime.GetRegionRevision(region);
    if (!before || !before_region_revision)
    {
        return false;
    }
    runtime.SetUpdatePolicy(std::make_shared<ThrowingPolicy>());
    const auto policy_failed = runtime.Update(EnvironmentUpdateInput{.game_delta = epidemic::runtime::GameDuration{1}, .region_id = region});
    if (policy_failed || !policy_failed.GetError().HasCode("environment.update_policy_exception") ||
        runtime.GetRevision() != before.Value().revision)
    {
        return false;
    }

    EnvironmentStateUpdate same{};
    same.region = region;
    same.source_revision = before_region_revision.Value();
    same.weather = before.Value().weather;
    same.season = before.Value().season;
    same.climate = before.Value().climate;
    const auto no_op = runtime.ApplyUpdate(same);
    return no_op && runtime.GetRevision() == before.Value().revision &&
           runtime.GetRegionRevision(region).Value() == before_region_revision.Value();
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
        {"DuplicateRegionRegistrationRejected", TestDuplicateRegionRegistrationRejected},
        {"NoOpSettersDoNotBumpRevision", TestNoOpSettersDoNotBumpRevision},
        {"SurfaceStateStoresRegionAndMixedConditions", TestSurfaceStateStoresRegionAndMixedConditions},
        {"SurfaceRegionOwnershipIsImmutable", TestSurfaceRegionOwnershipIsImmutable},
        {"ValidationRejectsInvalidValues", TestValidationRejectsInvalidValues},
        {"PartialRegionInitializationRejected", TestPartialRegionInitializationRejected},
        {"SnapshotAndProjectionAreRevisionedCopies", TestSnapshotAndProjectionAreRevisionedCopies},
        {"UpdateWithoutPolicyDoesNotMutateSurface", TestUpdateWithoutPolicyDoesNotMutateSurface},
        {"OptionalPolicyCanMutateSurface", TestOptionalPolicyCanMutateSurface},
        {"ApplyUpdateRejectsRevisionConflict", TestApplyUpdateRejectsRevisionConflict},
        {"BatchNormalizesWindDirection", TestBatchNormalizesWindDirection},
        {"InvalidBatchLeavesStateUnchanged", TestInvalidBatchLeavesStateUnchanged},
        {"EmptyUpdateDoesNotBumpRevision", TestEmptyUpdateDoesNotBumpRevision},
        {"CrossRegionRevisionDoesNotConflict", TestCrossRegionRevisionDoesNotConflict},
        {"FactoryCreatesSplitServices", TestFactoryCreatesSplitServices},
        {"BatchOwnershipAndDuplicatesAreTransactional", TestBatchOwnershipAndDuplicatesAreTransactional},
        {"EnvironmentEnumRevisionAllocationAndFreezeContracts", TestEnvironmentEnumRevisionAllocationAndFreezeContracts},
        {"FreezeRejectsNewSurfacesButAllowsExistingUpdates", TestFreezeRejectsNewSurfacesButAllowsExistingUpdates},
        {"RegistrationValidationAndWindNormalization", TestRegistrationValidationAndWindNormalization},
        {"FiniteNumericValidationIsTransactional", TestFiniteNumericValidationIsTransactional},
        {"AllocationFailuresPreserveStateAndRetry", TestAllocationFailuresPreserveStateAndRetry},
        {"RevisionExhaustionPreservesBatchStateAndNoOpStillSucceeds", TestRevisionExhaustionPreservesBatchStateAndNoOpStillSucceeds},
        {"PolicyResultAndInvalidOutputFailuresPreserveState", TestPolicyResultAndInvalidOutputFailuresPreserveState},
        {"EnvironmentPolicyExceptionAndNoOpBatch", TestEnvironmentPolicyExceptionAndNoOpBatch},
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

