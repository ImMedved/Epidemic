#include "Epidemic/GameFramework/RuntimeBridge/runtime_bridge.h"
#include "engine_runtime_bridge_backend.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/World/world_commands.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::runtime_bridge
{
namespace
{
foundation::Error E(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}
template <typename Integer>
foundation::Result<Integer> CheckedScaledInteger(float value)
{
    if (!std::isfinite(value))
    {
        return foundation::Result<Integer>::Failure(
            E("gameplay.runtime_bridge.invalid_runtime_observation", "runtime observation contains a non-finite numeric value"));
    }
    const long double scaled = static_cast<long double>(value) * 1000.0L;
    const long double rounded = std::round(scaled);
    const long double minimum = static_cast<long double>(std::numeric_limits<Integer>::min());
    const long double maximum = static_cast<long double>(std::numeric_limits<Integer>::max());
    if (!std::isfinite(scaled) || rounded < minimum || rounded > maximum)
    {
        return foundation::Result<Integer>::Failure(
            E("gameplay.runtime_bridge.invalid_runtime_observation", "runtime observation is outside the semantic numeric range"));
    }
    return foundation::Result<Integer>::Success(static_cast<Integer>(rounded));
}
RuntimeVector3 ToBridge(runtime::Vec3 value) noexcept
{
    return RuntimeVector3{value.x, value.y, value.z};
}
RuntimeQuaternion ToBridge(runtime::Quat value) noexcept
{
    return RuntimeQuaternion{value.x, value.y, value.z, value.w};
}
runtime::Vec3 ToRuntime(RuntimeVector3 value) noexcept
{
    return runtime::Vec3{value.x, value.y, value.z};
}
runtime::Vec3 ToRuntime(RuntimeWorldPosition value) noexcept
{
    return runtime::Vec3{static_cast<float>(value.x_mm) / 1000.0f,
                         static_cast<float>(value.y_mm) / 1000.0f,
                         static_cast<float>(value.z_mm) / 1000.0f};
}
foundation::Result<RuntimeWorldPosition> ToWorldPosition(runtime::Vec3 value)
{
    auto x = CheckedScaledInteger<std::int64_t>(value.x);
    auto y = CheckedScaledInteger<std::int64_t>(value.y);
    auto z = CheckedScaledInteger<std::int64_t>(value.z);
    if (!x) return foundation::Result<RuntimeWorldPosition>::Failure(x.GetError());
    if (!y) return foundation::Result<RuntimeWorldPosition>::Failure(y.GetError());
    if (!z) return foundation::Result<RuntimeWorldPosition>::Failure(z.GetError());
    return foundation::Result<RuntimeWorldPosition>::Success({x.Value(), y.Value(), z.Value()});
}

foundation::Result<RuntimeWorldPosition> ToWorldPosition(RuntimeVector3 value)
{
    auto x = CheckedScaledInteger<std::int64_t>(value.x);
    auto y = CheckedScaledInteger<std::int64_t>(value.y);
    auto z = CheckedScaledInteger<std::int64_t>(value.z);
    if (!x) return foundation::Result<RuntimeWorldPosition>::Failure(x.GetError());
    if (!y) return foundation::Result<RuntimeWorldPosition>::Failure(y.GetError());
    if (!z) return foundation::Result<RuntimeWorldPosition>::Failure(z.GetError());
    return foundation::Result<RuntimeWorldPosition>::Success({x.Value(), y.Value(), z.Value()});
}
bool TakeCounter(std::uint64_t& next, std::uint64_t& value) noexcept
{
    if (next == 0)
    {
        return false;
    }
    value = next;
    next = next == std::numeric_limits<std::uint64_t>::max() ? 0 : next + 1;
    return true;
}

RuntimeNavigationPathState ToBridge(runtime::navigation::PathQueryState state) noexcept
{
    switch (state)
    {
    case runtime::navigation::PathQueryState::Pending: return RuntimeNavigationPathState::Pending;
    case runtime::navigation::PathQueryState::Running: return RuntimeNavigationPathState::Running;
    case runtime::navigation::PathQueryState::PartiallyComplete: return RuntimeNavigationPathState::PartiallyComplete;
    case runtime::navigation::PathQueryState::Completed: return RuntimeNavigationPathState::Completed;
    case runtime::navigation::PathQueryState::Failed: return RuntimeNavigationPathState::Failed;
    case runtime::navigation::PathQueryState::Cancelled: return RuntimeNavigationPathState::Cancelled;
    case runtime::navigation::PathQueryState::Stale: return RuntimeNavigationPathState::Stale;
    }
    return RuntimeNavigationPathState::Failed;
}
} // namespace

RuntimePersistentObjectHandle EngineRuntimeBridgeBackend::ImportPersistentObjectId(runtime::PersistentObjectId id)
{
    std::uint64_t value = 0;
    if (!id.IsValid() || !TakeCounter(next_persistent_, value))
    {
        return {};
    }
    const RuntimePersistentObjectHandle handle{value};
    persistent_objects_.emplace(handle, id);
    return handle;
}

RuntimePhysicsBodyHandle EngineRuntimeBridgeBackend::ImportPhysicsBody(runtime::physics::PhysicsBodyHandle body)
{
    if (!body.IsValid())
    {
        return {};
    }
    const auto existing = physics_body_reverse_.find(body);
    if (existing != physics_body_reverse_.end())
    {
        return existing->second;
    }
    std::uint64_t value = 0;
    if (!TakeCounter(next_body_, value))
    {
        return {};
    }
    const RuntimePhysicsBodyHandle handle{value};
    physics_bodies_.emplace(handle, body);
    physics_body_reverse_.emplace(body, handle);
    return handle;
}

RuntimeRegionHandle EngineRuntimeBridgeBackend::ImportRegion(runtime::RegionId region)
{
    std::uint64_t value = 0;
    if (!region.IsValid() || !TakeCounter(next_region_, value))
    {
        return {};
    }
    const RuntimeRegionHandle handle{value};
    regions_.emplace(handle, region);
    return handle;
}

foundation::Result<void> EngineRuntimeBridgeBackend::ReleasePersistentObject(RuntimePersistentObjectHandle handle)
{
    if (!handle.IsValid() || persistent_objects_.erase(handle) == 0)
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.persistent_object_unknown", "runtime persistent object handle is unknown"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> EngineRuntimeBridgeBackend::ReleasePhysicsBody(RuntimePhysicsBodyHandle handle)
{
    const auto found = physics_bodies_.find(handle);
    if (found == physics_bodies_.end())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.physics_body_unknown", "runtime physics body handle is unknown"));
    }
    physics_body_reverse_.erase(found->second);
    physics_bodies_.erase(found);
    return foundation::Result<void>::Success();
}

foundation::Result<void> EngineRuntimeBridgeBackend::ReleaseRegion(RuntimeRegionHandle handle)
{
    if (!handle.IsValid() || regions_.erase(handle) == 0)
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.region_unknown", "runtime region handle is unknown"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<RuntimeMaterializationResult> EngineRuntimeBridgeBackend::Materialize(RuntimePersistentObjectHandle persistent_id)
{
    const auto persistent = persistent_objects_.find(persistent_id);
    if (persistent == persistent_objects_.end())
    {
        return foundation::Result<RuntimeMaterializationResult>::Failure(
            E("gameplay.runtime_bridge.persistent_object_unknown", "runtime persistent object handle is unknown"));
    }
    if (!world_.materialization)
    {
        return foundation::Result<RuntimeMaterializationResult>::Failure(
            E("gameplay.runtime_bridge.world_unavailable", "runtime world materializer is unavailable"));
    }
    if (next_object_ == 0)
    {
        return foundation::Result<RuntimeMaterializationResult>::Failure(
            E("gameplay.runtime_bridge.runtime_handle_exhausted", "runtime bridge object handle space is exhausted"));
    }
    auto materialized = world_.materialization->Materialize(
        runtime::MaterializationRequest{persistent->second, runtime::ObjectRealityLevel::Physical});
    if (!materialized)
    {
        return foundation::Result<RuntimeMaterializationResult>::Failure(materialized.GetError());
    }
    const auto existing = runtime_object_reverse_.find(materialized.Value());
    if (existing != runtime_object_reverse_.end())
    {
        return foundation::Result<RuntimeMaterializationResult>::Success(RuntimeMaterializationResult{existing->second, false});
    }
    std::uint64_t handle_value = 0;
    if (!TakeCounter(next_object_, handle_value))
    {
        return foundation::Result<RuntimeMaterializationResult>::Failure(
            E("gameplay.runtime_bridge.runtime_handle_exhausted", "runtime bridge object handle space is exhausted"));
    }
    const RuntimeObjectHandle handle{handle_value};
    runtime_objects_.emplace(handle, materialized.Value());
    runtime_object_reverse_.emplace(materialized.Value(), handle);
    return foundation::Result<RuntimeMaterializationResult>::Success(RuntimeMaterializationResult{handle, true});
}

foundation::Result<void> EngineRuntimeBridgeBackend::Dematerialize(RuntimeObjectHandle object)
{
    const auto actual = runtime_objects_.find(object);
    if (actual == runtime_objects_.end())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.runtime_object_missing", "runtime bridge object handle is unknown"));
    }
    if (!world_.query || !world_.materialization || !world_.demotion_authority)
    {
        return foundation::Result<void>::Failure(
            E("gameplay.runtime_bridge.world_unavailable", "runtime dematerialization services are unavailable"));
    }
    auto record = world_.query->FindObject(actual->second);
    if (!record)
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.runtime_object_missing", "runtime object is missing"));
    }
    runtime::DemotionSnapshot snapshot;
    snapshot.object = actual->second;
    snapshot.target_reality = runtime::ObjectRealityLevel::Logical;
    snapshot.collapsed_placement = record->placement;
    snapshot.collapse_record_id = foundation::StringId::FromString("framework.runtime_bridge.dematerialize");
    snapshot.source_revision = record->revision;
    auto token = world_.demotion_authority->IssueDemotionCommitToken(snapshot);
    if (!token)
    {
        return foundation::Result<void>::Failure(token.GetError());
    }
    auto result = world_.materialization->Demote(
        runtime::DemotionRequest{actual->second, runtime::ObjectRealityLevel::Logical, token.Value()});
    if (!result)
    {
        (void)world_.demotion_authority->RevokeDemotionCommitToken(token.Value().token_id);
        return result;
    }
    runtime_object_reverse_.erase(actual->second);
    runtime_objects_.erase(actual);
    return foundation::Result<void>::Success();
}

foundation::Result<void> EngineRuntimeBridgeBackend::Destroy(RuntimeObjectHandle object)
{
    const auto actual = runtime_objects_.find(object);
    if (actual == runtime_objects_.end())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.runtime_object_missing", "runtime bridge object handle is unknown"));
    }
    if (!world_.writer)
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.world_unavailable", "runtime world writer is unavailable"));
    }
    std::uint64_t revision = 0;
    if (world_.query)
    {
        auto record = world_.query->FindObject(actual->second);
        if (record)
        {
            revision = record->revision;
        }
    }
    auto result = world_.writer->Apply(
        runtime::DestroyObjectCommand{actual->second,
                                      revision,
                                      runtime::GameTimePoint{},
                                      foundation::StringId::FromString("framework.runtime_bridge.destroy")});
    if (!result)
    {
        return foundation::Result<void>::Failure(result.GetError());
    }
    runtime_object_reverse_.erase(actual->second);
    runtime_objects_.erase(actual);
    return foundation::Result<void>::Success();
}

foundation::Result<void> EngineRuntimeBridgeBackend::ApplyImpulse(RuntimePhysicsBodyHandle body, RuntimeVector3 impulse)
{
    const auto actual = physics_bodies_.find(body);
    if (actual == physics_bodies_.end())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.physics_body_unknown", "runtime physics body handle is unknown"));
    }
    if (!physics_.scene)
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.physics_unavailable", "runtime physics scene is unavailable"));
    }
    return physics_.scene->ApplyImpulse(actual->second, ToRuntime(impulse));
}

foundation::Result<void> EngineRuntimeBridgeBackend::ProjectEnvironment(
    RuntimeRegionHandle region, const RuntimeEnvironmentValues& values, Revision)
{
    const auto actual = regions_.find(region);
    if (actual == regions_.end())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.region_unknown", "runtime region handle is unknown"));
    }
    if (!environment_)
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.environment_unavailable", "runtime environment is unavailable"));
    }
    runtime::WeatherState weather{};
    weather.precipitation = static_cast<float>(values.precipitation_milli) / 1000.0f;
    weather.intensity = weather.precipitation;
    weather.wind_speed = static_cast<float>(values.wind_strength_milli) / 1000.0f;
    weather.current_temperature = static_cast<float>(values.temperature_milli_c) / 1000.0f;
    weather.current_humidity = static_cast<float>(values.humidity_milli) / 1000.0f;
    if (weather.precipitation > 0.75f)
    {
        weather.kind = runtime::WeatherKind::Storm;
    }
    else if (weather.precipitation > 0.05f)
    {
        weather.kind = runtime::WeatherKind::Rain;
    }
    else
    {
        weather.kind = runtime::WeatherKind::Clear;
    }
    return environment_->SetWeather(actual->second, weather);
}

std::vector<RuntimeContactObservation> EngineRuntimeBridgeBackend::ConsumeContacts()
{
    if (!physics_.events)
    {
        return {};
    }
    const auto contacts = physics_.events->Contacts();
    std::vector<RuntimeContactObservation> output;
    output.reserve(contacts.size());
    for (const auto& contact : contacts)
    {
        RuntimeContactObservation observation;
        if (const auto a = physics_body_reverse_.find(contact.a); a != physics_body_reverse_.end())
        {
            observation.a = a->second;
        }
        if (const auto b = physics_body_reverse_.find(contact.b); b != physics_body_reverse_.end())
        {
            observation.b = b->second;
        }
        observation.point = ToBridge(contact.point);
        observation.impulse = contact.impulse;
        output.push_back(observation);
    }
    physics_.events->Clear();
    return output;
}


foundation::Result<std::vector<RuntimeRayHit>> EngineRuntimeBridgeBackend::Raycast(const RuntimeRayQuery& query) const
{
    if (!physics_.query)
    {
        return foundation::Result<std::vector<RuntimeRayHit>>::Failure(
            E("gameplay.runtime_bridge.raycast_unsupported", "runtime physics query service is unavailable"));
    }
    const auto hit = physics_.query->Raycast(runtime::physics::RaycastQuery{ToRuntime(query.origin), ToRuntime(query.direction), query.max_distance});
    if (!hit)
    {
        return foundation::Result<std::vector<RuntimeRayHit>>::Failure(hit.GetError());
    }
    if (!hit.Value().hit)
    {
        return foundation::Result<std::vector<RuntimeRayHit>>::Success({});
    }
    const auto mapped = physics_body_reverse_.find(hit.Value().body);
    if (mapped == physics_body_reverse_.end())
    {
        return foundation::Result<std::vector<RuntimeRayHit>>::Success({});
    }
    return foundation::Result<std::vector<RuntimeRayHit>>::Success(
        {RuntimeRayHit{mapped->second, ToBridge(hit.Value().point), ToBridge(hit.Value().normal), hit.Value().distance}});
}

foundation::Result<std::vector<RuntimePhysicsBodyHandle>> EngineRuntimeBridgeBackend::Overlap(const RuntimeOverlapQuery& query) const
{
    if (!query.bounds.IsValid())
    {
        return foundation::Result<std::vector<RuntimePhysicsBodyHandle>>::Failure(
            E("gameplay.runtime_bridge.overlap_invalid", "runtime overlap bounds are invalid"));
    }
    if (!physics_.query)
    {
        return foundation::Result<std::vector<RuntimePhysicsBodyHandle>>::Failure(
            E("gameplay.runtime_bridge.overlap_unsupported", "runtime physics query service is unavailable"));
    }
    const runtime::Aabb bounds{ToRuntime(query.bounds.min), ToRuntime(query.bounds.max)};
    const auto overlap = physics_.query->Overlap(runtime::physics::OverlapQuery{bounds});
    if (!overlap)
    {
        return foundation::Result<std::vector<RuntimePhysicsBodyHandle>>::Failure(overlap.GetError());
    }
    std::vector<RuntimePhysicsBodyHandle> result;
    result.reserve(overlap.Value().bodies.size());
    for (const auto body : overlap.Value().bodies)
    {
        if (const auto mapped = physics_body_reverse_.find(body); mapped != physics_body_reverse_.end())
        {
            result.push_back(mapped->second);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return foundation::Result<std::vector<RuntimePhysicsBodyHandle>>::Success(std::move(result));
}

foundation::Result<bool> EngineRuntimeBridgeBackend::Visible(RuntimeObjectHandle observer, RuntimeObjectHandle target) const
{
    const auto observer_id = runtime_objects_.find(observer);
    const auto target_id = runtime_objects_.find(target);
    if (observer_id == runtime_objects_.end() || target_id == runtime_objects_.end())
    {
        return foundation::Result<bool>::Failure(E("gameplay.runtime_bridge.runtime_object_missing", "visibility object handle is unknown"));
    }
    if (!world_.query || !physics_.query || !physics_.scene)
    {
        return foundation::Result<bool>::Failure(E("gameplay.runtime_bridge.visibility_unsupported", "runtime visibility dependencies are unavailable"));
    }
    const auto observer_record = world_.query->FindObject(observer_id->second);
    const auto target_record = world_.query->FindObject(target_id->second);
    if (!observer_record || !target_record)
    {
        return foundation::Result<bool>::Failure(E("gameplay.runtime_bridge.runtime_object_missing", "visibility object is not present in runtime world"));
    }
    const auto* observer_placement = std::get_if<runtime::WorldSurfacePlacement>(&observer_record->placement);
    const auto* target_placement = std::get_if<runtime::WorldSurfacePlacement>(&target_record->placement);
    if (observer_placement == nullptr || target_placement == nullptr)
    {
        return foundation::Result<bool>::Failure(E("gameplay.runtime_bridge.visibility_unsupported", "visibility requires world-surface placements"));
    }
    const auto delta = target_placement->transform.position - observer_placement->transform.position;
    const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (distance <= runtime::kSpatialEpsilon)
    {
        return foundation::Result<bool>::Success(true);
    }
    const runtime::Vec3 direction{delta.x / distance, delta.y / distance, delta.z / distance};
    const auto ray = physics_.query->Raycast(runtime::physics::RaycastQuery{observer_placement->transform.position, direction, distance});
    if (!ray)
    {
        return foundation::Result<bool>::Failure(ray.GetError());
    }
    if (!ray.Value().hit)
    {
        return foundation::Result<bool>::Success(true);
    }
    const auto body = physics_.scene->GetBodySnapshot(ray.Value().body);
    if (!body)
    {
        return foundation::Result<bool>::Failure(body.GetError());
    }
    return foundation::Result<bool>::Success(body.Value().owner == target_id->second);
}

foundation::Result<RuntimeTransformObservation> EngineRuntimeBridgeBackend::ObserveTransform(RuntimeObjectHandle object) const
{
    const auto actual = runtime_objects_.find(object);
    if (actual == runtime_objects_.end())
    {
        return foundation::Result<RuntimeTransformObservation>::Failure(
            E("gameplay.runtime_bridge.runtime_object_missing", "runtime bridge object handle is unknown"));
    }
    if (!world_.query)
    {
        return foundation::Result<RuntimeTransformObservation>::Failure(
            E("gameplay.runtime_bridge.transform_unsupported", "runtime world query service is unavailable"));
    }
    const auto record = world_.query->FindObject(actual->second);
    if (!record)
    {
        return foundation::Result<RuntimeTransformObservation>::Failure(
            E("gameplay.runtime_bridge.runtime_object_missing", "runtime object is not present in runtime world"));
    }
    const auto* placement = std::get_if<runtime::WorldSurfacePlacement>(&record->placement);
    if (placement == nullptr)
    {
        return foundation::Result<RuntimeTransformObservation>::Failure(
            E("gameplay.runtime_bridge.transform_unsupported", "runtime object does not have a world-surface transform"));
    }
    RuntimeRegionHandle region{};
    for (const auto& [handle, runtime_region] : regions_)
    {
        if (runtime_region == placement->region)
        {
            region = handle;
            break;
        }
    }
    auto semantic_position = ToWorldPosition(placement->transform.position);
    if (!semantic_position)
    {
        return foundation::Result<RuntimeTransformObservation>::Failure(semantic_position.GetError());
    }
    RuntimeTransformObservation observation;
    observation.position = semantic_position.Value();
    observation.rotation = ToBridge(placement->transform.rotation);
    observation.scale = ToBridge(placement->transform.scale);
    observation.region = region;
    return foundation::Result<RuntimeTransformObservation>::Success(observation);
}

foundation::Result<bool> EngineRuntimeBridgeBackend::IsRepresentationAlive(RuntimeObjectHandle object) const
{
    const auto actual = runtime_objects_.find(object);
    if (actual == runtime_objects_.end())
    {
        return foundation::Result<bool>::Success(false);
    }
    if (!world_.query)
    {
        return foundation::Result<bool>::Failure(
            E("gameplay.runtime_bridge.reconciliation_unsupported", "runtime world query service is unavailable"));
    }
    return foundation::Result<bool>::Success(world_.query->FindObject(actual->second).has_value());
}

foundation::Result<RuntimeNavigationQueryHandle> EngineRuntimeBridgeBackend::RequestPath(const RuntimeNavigationPathQuery& query)
{
    if (!navigation_.runtime)
    {
        return foundation::Result<RuntimeNavigationQueryHandle>::Failure(
            E("gameplay.runtime_bridge.navigation_unsupported", "runtime navigation service is unavailable"));
    }
    const auto region = regions_.find(query.region);
    if (region == regions_.end())
    {
        return foundation::Result<RuntimeNavigationQueryHandle>::Failure(E("gameplay.runtime_bridge.region_unknown", "navigation region handle is unknown"));
    }
    if (next_path_ == 0)
    {
        return foundation::Result<RuntimeNavigationQueryHandle>::Failure(
            E("gameplay.runtime_bridge.navigation_handle_exhausted", "navigation bridge handle space is exhausted"));
    }
    runtime::navigation::PathRequest request;
    request.start = ToRuntime(query.from);
    request.target = ToRuntime(query.to);
    request.region = region->second;
    request.source_revision = query.source_revision;
    const auto actual = navigation_.runtime->RequestPathHandle(request);
    if (!actual)
    {
        return foundation::Result<RuntimeNavigationQueryHandle>::Failure(actual.GetError());
    }
    std::uint64_t handle_value = 0;
    if (!TakeCounter(next_path_, handle_value))
    {
        return foundation::Result<RuntimeNavigationQueryHandle>::Failure(
            E("gameplay.runtime_bridge.navigation_handle_exhausted", "navigation bridge handle space is exhausted"));
    }
    const RuntimeNavigationQueryHandle handle{handle_value};
    paths_.emplace(handle.value, actual.Value());
    return foundation::Result<RuntimeNavigationQueryHandle>::Success(handle);
}

foundation::Result<RuntimeNavigationPathState> EngineRuntimeBridgeBackend::GetPathState(RuntimeNavigationQueryHandle handle) const
{
    const auto actual = paths_.find(handle.value);
    if (!handle.IsValid() || actual == paths_.end() || !navigation_.runtime)
    {
        return foundation::Result<RuntimeNavigationPathState>::Failure(E("gameplay.runtime_bridge.navigation_handle_invalid", "navigation bridge handle is invalid"));
    }
    const auto state = navigation_.runtime->GetPathState(actual->second);
    if (!state)
    {
        return foundation::Result<RuntimeNavigationPathState>::Failure(state.GetError());
    }
    return foundation::Result<RuntimeNavigationPathState>::Success(ToBridge(state.Value()));
}

foundation::Result<RuntimeNavigationPath> EngineRuntimeBridgeBackend::GetPathResult(RuntimeNavigationQueryHandle handle) const
{
    const auto actual = paths_.find(handle.value);
    if (!handle.IsValid() || actual == paths_.end() || !navigation_.runtime)
    {
        return foundation::Result<RuntimeNavigationPath>::Failure(E("gameplay.runtime_bridge.navigation_handle_invalid", "navigation bridge handle is invalid"));
    }
    const auto result = navigation_.runtime->GetPathResult(actual->second);
    if (!result)
    {
        return foundation::Result<RuntimeNavigationPath>::Failure(result.GetError());
    }
    RuntimeNavigationPath output;
    output.state = ToBridge(result.Value().state);
    output.navigation_revision = result.Value().nav_revision;
    output.result_revision = result.Value().revision;
    output.points.reserve(result.Value().points.size());
    for (const auto point : result.Value().points)
    {
        auto semantic_point = ToWorldPosition(point);
        if (!semantic_point)
        {
            return foundation::Result<RuntimeNavigationPath>::Failure(semantic_point.GetError());
        }
        output.points.push_back(semantic_point.Value());
    }
    return foundation::Result<RuntimeNavigationPath>::Success(std::move(output));
}

foundation::Result<void> EngineRuntimeBridgeBackend::CancelPath(RuntimeNavigationQueryHandle handle)
{
    const auto actual = paths_.find(handle.value);
    if (!handle.IsValid() || actual == paths_.end() || !navigation_.runtime)
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.navigation_handle_invalid", "navigation bridge handle is invalid"));
    }
    return navigation_.runtime->CancelPath(actual->second);
}

foundation::Result<void> EngineRuntimeBridgeBackend::ReleasePath(RuntimeNavigationQueryHandle handle)
{
    const auto actual = paths_.find(handle.value);
    if (!handle.IsValid() || actual == paths_.end() || !navigation_.runtime)
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.navigation_handle_invalid", "navigation bridge handle is invalid"));
    }
    const auto released = navigation_.runtime->ReleasePathResult(actual->second);
    if (!released)
    {
        return released;
    }
    paths_.erase(actual);
    return foundation::Result<void>::Success();
}

foundation::Result<RuntimeEnvironmentSample> EngineRuntimeBridgeBackend::SampleEnvironment(RuntimeRegionHandle region) const
{
    const auto actual = regions_.find(region);
    if (actual == regions_.end())
    {
        return foundation::Result<RuntimeEnvironmentSample>::Failure(E("gameplay.runtime_bridge.region_unknown", "runtime region handle is unknown"));
    }
    if (!environment_)
    {
        return foundation::Result<RuntimeEnvironmentSample>::Failure(E("gameplay.runtime_bridge.environment_sample_unsupported", "runtime environment service is unavailable"));
    }
    const auto weather = environment_->GetWeather(actual->second);
    const auto revision = environment_->GetRegionRevision(actual->second);
    if (!weather)
    {
        return foundation::Result<RuntimeEnvironmentSample>::Failure(weather.GetError());
    }
    if (!revision)
    {
        return foundation::Result<RuntimeEnvironmentSample>::Failure(revision.GetError());
    }
    auto temperature = CheckedScaledInteger<std::int32_t>(weather.Value().current_temperature);
    auto humidity = CheckedScaledInteger<std::int32_t>(weather.Value().current_humidity);
    auto precipitation = CheckedScaledInteger<std::int32_t>(weather.Value().precipitation);
    auto wind = CheckedScaledInteger<std::int32_t>(weather.Value().wind_speed);
    if (!temperature) return foundation::Result<RuntimeEnvironmentSample>::Failure(temperature.GetError());
    if (!humidity) return foundation::Result<RuntimeEnvironmentSample>::Failure(humidity.GetError());
    if (!precipitation) return foundation::Result<RuntimeEnvironmentSample>::Failure(precipitation.GetError());
    if (!wind) return foundation::Result<RuntimeEnvironmentSample>::Failure(wind.GetError());
    RuntimeEnvironmentValues values;
    values.temperature_milli_c = temperature.Value();
    values.humidity_milli = humidity.Value();
    values.precipitation_milli = precipitation.Value();
    values.wind_strength_milli = wind.Value();
    return foundation::Result<RuntimeEnvironmentSample>::Success({values, Revision{revision.Value()}});
}

RuntimeProjectionPriority RuntimeBridgeService::PriorityOf(const RuntimeProjectionRequest& request)
{
    return std::visit([](const auto& value) { return value.priority; }, request);
}

foundation::Result<void> RuntimeBridgeService::Enqueue(RuntimeProjectionRequest request)
{
    auto valid = std::visit(
        [this](const auto& value) -> foundation::Result<void> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, MaterializeRequest>)
            {
                if (!value.object.IsValid() || !value.persistent_id.IsValid())
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.invalid_request", "materialize request is invalid"));
                }
            }
            else if constexpr (std::is_same_v<T, DematerializeRequest> || std::is_same_v<T, DestroyRuntimeRequest>)
            {
                if (!value.object.IsValid() || !value.generation.IsValid())
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.invalid_request", "runtime binding request is invalid"));
                }
            }
            else if constexpr (std::is_same_v<T, ImpulseProjectionRequest>)
            {
                if (!value.object.IsValid() || !value.generation.IsValid() ||
                    (value.part.part.IsValid() && !value.part.object.IsValid()) ||
                    (value.part.object.IsValid() && value.part.object != value.object))
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.invalid_request", "impulse request is invalid"));
                }
            }
            else if constexpr (std::is_same_v<T, EnvironmentProjectionRequest>)
            {
                if (!value.region.IsValid())
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.invalid_request", "environment request region is invalid"));
                }
                const auto capabilities = backend_.GetCapabilities();
                if ((!capabilities.environment_temperature && value.values.temperature_milli_c != 0) ||
                    (!capabilities.environment_humidity && value.values.humidity_milli != 0) ||
                    (!capabilities.environment_precipitation && value.values.precipitation_milli != 0) ||
                    (!capabilities.environment_wind && value.values.wind_strength_milli != 0) ||
                    (!capabilities.environment_visibility && value.values.visibility_milli != 1000) ||
                    (!capabilities.environment_light_exposure && value.values.light_exposure_milli != 1000))
                {
                    return foundation::Result<void>::Failure(
                        E("gameplay.runtime_bridge.environment_projection_unsupported",
                          "runtime backend cannot project one or more requested environment fields"));
                }
            }
            else
            {
                if (!value.alteration.IsValid() || !value.type.IsValid() || !value.area.IsValid())
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.invalid_request", "world alteration request is invalid"));
                }
                if (!backend_.GetCapabilities().world_alteration_projection)
                {
                    return foundation::Result<void>::Failure(
                        E("gameplay.runtime_bridge.world_projection_unsupported", "runtime backend does not support world alteration projection"));
                }
            }
            return foundation::Result<void>::Success();
        },
        request);
    if (!valid)
    {
        return valid;
    }

    // Replaceable projections keep their original queue sequence so that coalescing cannot reorder unrelated commands.
    for (auto& queued : queue_)
    {
        const bool same_environment = std::holds_alternative<EnvironmentProjectionRequest>(queued.request) &&
                                      std::holds_alternative<EnvironmentProjectionRequest>(request) &&
                                      std::get<EnvironmentProjectionRequest>(queued.request).region ==
                                          std::get<EnvironmentProjectionRequest>(request).region;
        const bool same_world = std::holds_alternative<WorldAlterationProjectionRequest>(queued.request) &&
                                std::holds_alternative<WorldAlterationProjectionRequest>(request) &&
                                std::get<WorldAlterationProjectionRequest>(queued.request).alteration ==
                                    std::get<WorldAlterationProjectionRequest>(request).alteration;
        if (same_environment || same_world)
        {
            const auto queued_revision = std::visit([](const auto& value) -> Revision {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, EnvironmentProjectionRequest> || std::is_same_v<T, WorldAlterationProjectionRequest>)
                {
                    return value.gameplay_revision;
                }
                return {};
            }, queued.request);
            const auto incoming_revision = std::visit([](const auto& value) -> Revision {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, EnvironmentProjectionRequest> || std::is_same_v<T, WorldAlterationProjectionRequest>)
                {
                    return value.gameplay_revision;
                }
                return {};
            }, request);
            if (incoming_revision.value == 0 || queued_revision.value == 0 || incoming_revision >= queued_revision)
            {
                queued.priority = PriorityOf(request);
                queued.request = std::move(request);
                queued.attempts = 0;
                queued.last_error.reset();
            }
            ++projection_requests_;
            ++coalesced_projection_requests_;
            return foundation::Result<void>::Success();
        }
    }

    if (queue_.size() >= queue_policy_.max_projection_requests)
    {
        ++rejected_projection_requests_;
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.queue_full", "runtime projection queue capacity is exhausted"));
    }
    std::uint64_t sequence = 0;
    if (!TakeCounter(next_sequence_, sequence))
    {
        ++rejected_projection_requests_;
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.sequence_exhausted", "projection queue sequence is exhausted"));
    }
    queue_.push_back(Queued{sequence, PriorityOf(request), std::move(request), 0, std::nullopt});
    ++projection_requests_;
    return foundation::Result<void>::Success();
}

std::optional<RuntimeBinding> RuntimeBridgeService::GetBinding(GameplayObjectRef object) const noexcept
{
    const auto found = bindings_.find(object);
    return found == bindings_.end() ? std::nullopt : std::optional<RuntimeBinding>{found->second};
}

std::optional<RuntimeBinding> RuntimeBridgeService::GetBinding(RuntimeObjectHandle object) const noexcept
{
    const auto found = reverse_.find(object);
    return found == reverse_.end() ? std::nullopt : GetBinding(found->second);
}

void RuntimeBridgeService::RemoveBinding(GameplayObjectRef object)
{
    const auto found = bindings_.find(object);
    if (found == bindings_.end())
    {
        return;
    }
    for (const auto& attachment : found->second.physics_attachments)
    {
        body_reverse_.erase(attachment.body);
    }
    reverse_.erase(found->second.runtime_object);
    bindings_.erase(found);
}

foundation::Result<void> RuntimeBridgeService::AttachPhysicsBody(
    GameplayObjectRef object, RuntimeBindingGeneration generation, RuntimePhysicsBodyHandle body, GameplayObjectPartRef part)
{
    auto found = bindings_.find(object);
    if (found == bindings_.end() || found->second.generation != generation || !body.IsValid())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_stale", "binding missing or stale"));
    }
    if ((part.part.IsValid() && !part.object.IsValid()) || (part.object.IsValid() && part.object != object))
    {
        return foundation::Result<void>::Failure(
            E("gameplay.runtime_bridge.part_invalid", "physics attachment part must belong to the bound gameplay object"));
    }
    const auto body_owner = body_reverse_.find(body);
    if (body_owner != body_reverse_.end())
    {
        if (body_owner->second.object == object && body_owner->second.part == part)
        {
            return foundation::Result<void>::Success();
        }
        return foundation::Result<void>::Failure(
            E("gameplay.runtime_bridge.body_collision", "physics body is already attached to another gameplay object or part"));
    }
    found->second.physics_attachments.push_back(RuntimePhysicsAttachment{body, part});
    std::sort(found->second.physics_attachments.begin(), found->second.physics_attachments.end(), [](const auto& left, const auto& right) {
        if (left.part.object != right.part.object) return left.part.object < right.part.object;
        if (left.part.part != right.part.part) return left.part.part < right.part.part;
        return left.body < right.body;
    });
    body_reverse_.emplace(body, BodyOwner{object, part});
    return foundation::Result<void>::Success();
}

foundation::Result<void> RuntimeBridgeService::ProcessOne(const RuntimeProjectionRequest& request, RuntimeBridgeProcessResult& output)
{
    return std::visit(
        [&](const auto& value) -> foundation::Result<void> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, MaterializeRequest>)
            {
                auto existing = bindings_.find(value.object);
                if (existing != bindings_.end())
                {
                    if (existing->second.gameplay_revision >= value.gameplay_revision)
                    {
                        return foundation::Result<void>::Success();
                    }
                    if (existing->second.persistent_object != value.persistent_id)
                    {
                        return foundation::Result<void>::Failure(
                            E("gameplay.runtime_bridge.persistent_identity_changed",
                              "materialized gameplay object cannot change runtime persistent identity without explicit dematerialization"));
                    }
                    existing->second.gameplay_revision = value.gameplay_revision;
                    return foundation::Result<void>::Success();
                }

                auto& generation_value = generations_[value.object];
                if (generation_value == std::numeric_limits<std::uint32_t>::max())
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.generation_exhausted", "binding generation is exhausted"));
                }
                auto materialized = backend_.Materialize(value.persistent_id);
                if (!materialized || !materialized.Value().object.IsValid())
                {
                    return foundation::Result<void>::Failure(
                        materialized ? E("gameplay.runtime_bridge.materialize_invalid", "runtime backend returned an invalid object handle")
                                     : materialized.GetError());
                }
                const auto runtime_owner = reverse_.find(materialized.Value().object);
                if (runtime_owner != reverse_.end() && runtime_owner->second != value.object)
                {
                    if (materialized.Value().created)
                    {
                        const auto rollback = backend_.Dematerialize(materialized.Value().object);
                        if (!rollback)
                        {
                            return foundation::Result<void>::Failure(
                                E("gameplay.runtime_bridge.materialize_rollback_failed",
                                  "runtime object collision occurred and the newly created representation could not be rolled back"));
                        }
                    }
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.runtime_collision", "runtime object is already bound"));
                }
                ++generation_value;
                RuntimeBinding binding{value.object,
                                       value.persistent_id,
                                       materialized.Value().object,
                                       {},
                                       RuntimeBindingGeneration{generation_value},
                                       RuntimeBindingState::Active,
                                       value.gameplay_revision};
                bindings_.emplace(value.object, binding);
                reverse_[binding.runtime_object] = value.object;
                ++materializations_;
                ++output.materialized;
                return foundation::Result<void>::Success();
            }
            else if constexpr (std::is_same_v<T, DematerializeRequest>)
            {
                auto found = bindings_.find(value.object);
                if (found == bindings_.end() || found->second.generation != value.generation)
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_stale", "dematerialize binding is stale"));
                }
                auto result = backend_.Dematerialize(found->second.runtime_object);
                if (!result)
                {
                    return result;
                }
                RemoveBinding(value.object);
                ++dematerializations_;
                ++output.dematerialized;
                return foundation::Result<void>::Success();
            }
            else if constexpr (std::is_same_v<T, DestroyRuntimeRequest>)
            {
                auto found = bindings_.find(value.object);
                if (found == bindings_.end() || found->second.generation != value.generation)
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_stale", "destroy binding is stale"));
                }
                auto result = backend_.Destroy(found->second.runtime_object);
                if (!result)
                {
                    return result;
                }
                RemoveBinding(value.object);
                ++output.destroyed;
                return foundation::Result<void>::Success();
            }
            else if constexpr (std::is_same_v<T, ImpulseProjectionRequest>)
            {
                auto found = bindings_.find(value.object);
                if (found == bindings_.end() || found->second.generation != value.generation)
                {
                    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_stale", "impulse binding is stale"));
                }
                const RuntimePhysicsAttachment* selected = nullptr;
                for (const auto& attachment : found->second.physics_attachments)
                {
                    const bool matches = value.part.IsValid() ? attachment.part == value.part : !attachment.part.IsValid();
                    if (!matches) continue;
                    if (selected)
                    {
                        return foundation::Result<void>::Failure(
                            E("gameplay.runtime_bridge.physics_body_ambiguous", "multiple physics bodies match the requested gameplay object or part"));
                    }
                    selected = &attachment;
                }
                if (!selected && !value.part.IsValid() && found->second.physics_attachments.size() == 1)
                {
                    selected = &found->second.physics_attachments.front();
                }
                if (!selected)
                {
                    return foundation::Result<void>::Failure(
                        E("gameplay.runtime_bridge.physics_body_missing", "no physics body matches the requested gameplay object or part"));
                }
                return backend_.ApplyImpulse(selected->body, value.impulse);
            }
            else if constexpr (std::is_same_v<T, EnvironmentProjectionRequest>)
            {
                auto& revision = environment_projection_revision_[value.region.value];
                if (revision >= value.gameplay_revision && value.gameplay_revision.value != 0)
                {
                    return foundation::Result<void>::Success();
                }
                auto result = backend_.ProjectEnvironment(value.region, value.values, value.gameplay_revision);
                if (result)
                {
                    revision = value.gameplay_revision;
                }
                return result;
            }
            else
            {
                auto& revision = world_projection_revision_[value.alteration];
                if (revision >= value.gameplay_revision && value.gameplay_revision.value != 0)
                {
                    return foundation::Result<void>::Success();
                }
                auto result = backend_.ProjectWorldAlteration(value);
                if (result)
                {
                    revision = value.gameplay_revision;
                }
                return result;
            }
        },
        request);
}

RuntimeBridgeProcessResult RuntimeBridgeService::Process(RuntimeBridgeBudget budget)
{
    std::stable_sort(queue_.begin(), queue_.end(), [](const Queued& left, const Queued& right) {
        if (left.priority != right.priority)
        {
            return left.priority < right.priority;
        }
        return left.sequence < right.sequence;
    });
    RuntimeBridgeProcessResult output;
    std::vector<Queued> remaining;
    remaining.reserve(queue_.size());
    std::uint32_t materializations = 0;
    const auto max_attempts = std::max<std::uint32_t>(1, queue_policy_.max_projection_attempts);
    for (auto& queued : queue_)
    {
        if (queued.attempts >= max_attempts)
        {
            if (reconciliation_.size() < queue_policy_.max_reconciliation_requests)
            {
                reconciliation_.push_back(RuntimeProjectionReconciliationRecord{
                    queued.sequence, queued.priority, std::move(queued.request), queued.attempts, std::move(queued.last_error)});
            }
            else
            {
                remaining.push_back(std::move(queued));
                ++output.deferred;
            }
            continue;
        }

        const bool is_materialization = std::holds_alternative<MaterializeRequest>(queued.request);
        if (output.processed >= budget.max_commands || (is_materialization && materializations >= budget.max_materializations))
        {
            remaining.push_back(std::move(queued));
            ++output.deferred;
            continue;
        }
        auto result = ProcessOne(queued.request, output);
        ++output.processed;
        if (is_materialization)
        {
            ++materializations;
        }
        if (!result)
        {
            ++output.failed;
            ++projection_failures_;
            ++queued.attempts;
            queued.last_error = result.GetError();
            if (queued.attempts >= max_attempts && reconciliation_.size() < queue_policy_.max_reconciliation_requests)
            {
                reconciliation_.push_back(RuntimeProjectionReconciliationRecord{
                    queued.sequence, queued.priority, std::move(queued.request), queued.attempts, std::move(queued.last_error)});
            }
            else
            {
                remaining.push_back(std::move(queued));
            }
        }
    }
    queue_ = std::move(remaining);
    return output;
}

std::vector<SemanticImpactObservation> RuntimeBridgeService::CollectImpactObservations(GameplayTickId tick, RuntimeBridgeBudget budget)
{
    auto fresh = backend_.ConsumeContacts();
    const auto available = queue_policy_.max_contact_backlog > contact_backlog_.size()
                               ? queue_policy_.max_contact_backlog - contact_backlog_.size()
                               : 0;
    const auto accepted = std::min<std::size_t>(available, fresh.size());
    contact_backlog_.insert(contact_backlog_.end(), fresh.begin(), fresh.begin() + static_cast<std::ptrdiff_t>(accepted));
    dropped_contacts_ += fresh.size() - accepted;
    std::vector<SemanticImpactObservation> output;
    output.reserve(std::min<std::size_t>(contact_backlog_.size(), budget.max_observations));
    std::vector<RuntimeContactObservation> remaining;
    for (const auto& contact : contact_backlog_)
    {
        if (output.size() >= budget.max_observations)
        {
            remaining.push_back(contact);
            continue;
        }
        const auto a = body_reverse_.find(contact.a);
        const auto b = body_reverse_.find(contact.b);
        if (a == body_reverse_.end() && b == body_reverse_.end())
        {
            ++stale_observations_;
            continue;
        }
        auto semantic_point = ToWorldPosition(contact.point);
        auto semantic_impulse = CheckedScaledInteger<std::int64_t>(contact.impulse);
        if (!semantic_point || !semantic_impulse)
        {
            ++invalid_runtime_observations_;
            continue;
        }
        SemanticImpactObservation observation;
        observation.observed_tick = tick;
        observation.point = semantic_point.Value();
        observation.impulse_milli = semantic_impulse.Value();
        if (a != body_reverse_.end())
        {
            observation.subject = a->second.object;
            observation.subject_part = a->second.part;
            if (const auto binding = GetBinding(observation.subject))
            {
                observation.subject_generation = binding->generation;
            }
        }
        if (b != body_reverse_.end())
        {
            observation.other = b->second.object;
            observation.other_part = b->second.part;
            if (const auto binding = GetBinding(observation.other))
            {
                observation.other_generation = binding->generation;
            }
        }
        output.push_back(observation);
        ++observations_;
    }
    contact_backlog_ = std::move(remaining);
    return output;
}

foundation::Result<std::vector<SemanticRayHit>> RuntimeBridgeService::Raycast(const RuntimeRayQuery& query) const
{
    auto hits = backend_.Raycast(query);
    if (!hits)
    {
        return foundation::Result<std::vector<SemanticRayHit>>::Failure(hits.GetError());
    }
    std::vector<SemanticRayHit> output;
    for (const auto& hit : hits.Value())
    {
        const auto owner = body_reverse_.find(hit.body);
        if (owner == body_reverse_.end())
        {
            continue;
        }
        const auto binding = GetBinding(owner->second.object);
        if (!binding)
        {
            continue;
        }
        auto semantic_point = ToWorldPosition(hit.point);
        auto semantic_distance = CheckedScaledInteger<std::int64_t>(hit.distance);
        if (!semantic_point) return foundation::Result<std::vector<SemanticRayHit>>::Failure(semantic_point.GetError());
        if (!semantic_distance) return foundation::Result<std::vector<SemanticRayHit>>::Failure(semantic_distance.GetError());
        output.push_back(SemanticRayHit{owner->second.object,
                                        owner->second.part,
                                        binding->generation,
                                        semantic_point.Value(),
                                        semantic_distance.Value()});
    }
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        if (left.distance_milli != right.distance_milli)
        {
            return left.distance_milli < right.distance_milli;
        }
        return left.object < right.object;
    });
    return foundation::Result<std::vector<SemanticRayHit>>::Success(std::move(output));
}

foundation::Result<std::vector<SemanticOverlapHit>> RuntimeBridgeService::Overlap(const RuntimeOverlapQuery& query) const
{
    if (!query.bounds.IsValid())
    {
        return foundation::Result<std::vector<SemanticOverlapHit>>::Failure(
            E("gameplay.runtime_bridge.overlap_invalid", "overlap bounds are invalid"));
    }
    auto bodies = backend_.Overlap(query);
    if (!bodies)
    {
        return foundation::Result<std::vector<SemanticOverlapHit>>::Failure(bodies.GetError());
    }
    std::vector<SemanticOverlapHit> output;
    std::unordered_set<GameplayObjectRef> seen_objects;
    std::unordered_set<GameplayObjectPartRef> seen_parts;
    for (const auto body : bodies.Value())
    {
        const auto owner = body_reverse_.find(body);
        if (owner == body_reverse_.end())
        {
            continue;
        }
        if (owner->second.part.IsValid())
        {
            if (!seen_parts.insert(owner->second.part).second) continue;
        }
        else if (!seen_objects.insert(owner->second.object).second)
        {
            continue;
        }
        const auto binding = GetBinding(owner->second.object);
        if (binding)
        {
            output.push_back(SemanticOverlapHit{owner->second.object, owner->second.part, binding->generation});
        }
    }
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        if (left.object != right.object) return left.object < right.object;
        return left.part.part < right.part.part;
    });
    return foundation::Result<std::vector<SemanticOverlapHit>>::Success(std::move(output));
}

foundation::Result<bool> RuntimeBridgeService::Visible(GameplayObjectRef observer, GameplayObjectRef target) const
{
    const auto observer_binding = GetBinding(observer);
    const auto target_binding = GetBinding(target);
    if (!observer_binding || !target_binding)
    {
        return foundation::Result<bool>::Failure(
            E("gameplay.runtime_bridge.binding_missing", "visibility query requires both gameplay objects to be materialized"));
    }
    return backend_.Visible(observer_binding->runtime_object, target_binding->runtime_object);
}

foundation::Result<RuntimeTransformObservation> RuntimeBridgeService::ObserveTransform(GameplayObjectRef object) const
{
    const auto binding = GetBinding(object);
    if (!binding)
    {
        return foundation::Result<RuntimeTransformObservation>::Failure(
            E("gameplay.runtime_bridge.binding_missing", "transform query requires a materialized gameplay object"));
    }
    auto observed = backend_.ObserveTransform(binding->runtime_object);
    if (!observed)
    {
        return foundation::Result<RuntimeTransformObservation>::Failure(observed.GetError());
    }
    auto value = observed.Value();
    value.generation = binding->generation;
    return foundation::Result<RuntimeTransformObservation>::Success(std::move(value));
}

foundation::Result<void> RuntimeBridgeService::ForgetObjectIdentity(GameplayObjectRef object)
{
    if (!object.IsValid())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.object_invalid", "gameplay object identity is invalid"));
    }
    if (bindings_.contains(object))
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_active", "cannot forget identity while a runtime binding is active"));
    }
    for (const auto& queued : queue_)
    {
        const bool references = std::visit([object](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, MaterializeRequest> || std::is_same_v<T, DematerializeRequest> ||
                          std::is_same_v<T, DestroyRuntimeRequest> || std::is_same_v<T, ImpulseProjectionRequest>)
            {
                return value.object == object;
            }
            return false;
        }, queued.request);
        if (references)
        {
            return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.identity_in_use", "queued runtime command still references gameplay object identity"));
        }
    }
    generations_.erase(object);
    return foundation::Result<void>::Success();
}

foundation::Result<void> RuntimeBridgeService::ForgetEnvironmentProjection(RuntimeRegionHandle region)
{
    if (!region.IsValid())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.region_invalid", "runtime region handle is invalid"));
    }
    for (const auto& queued : queue_)
    {
        if (const auto* request = std::get_if<EnvironmentProjectionRequest>(&queued.request); request && request->region == region)
        {
            return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.projection_in_use", "queued environment projection still references region"));
        }
    }
    environment_projection_revision_.erase(region.value);
    return foundation::Result<void>::Success();
}

foundation::Result<void> RuntimeBridgeService::ForgetWorldAlterationProjection(GameplayObjectId alteration)
{
    if (!alteration.IsValid())
    {
        return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.alteration_invalid", "world alteration identity is invalid"));
    }
    for (const auto& queued : queue_)
    {
        if (const auto* request = std::get_if<WorldAlterationProjectionRequest>(&queued.request); request && request->alteration == alteration)
        {
            return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.projection_in_use", "queued world projection still references alteration"));
        }
    }
    world_projection_revision_.erase(alteration);
    return foundation::Result<void>::Success();
}

std::vector<RuntimeProjectionReconciliationRecord> RuntimeBridgeService::ReconciliationRequests() const
{
    auto records = reconciliation_;
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) { return left.sequence < right.sequence; });
    return records;
}

foundation::Result<void> RuntimeBridgeService::RetryReconciliation(std::uint64_t sequence)
{
    const auto found = std::find_if(reconciliation_.begin(), reconciliation_.end(),
                                    [sequence](const auto& record) { return record.sequence == sequence; });
    if (found == reconciliation_.end())
    {
        return foundation::Result<void>::Failure(
            E("gameplay.runtime_bridge.reconciliation_missing", "runtime projection reconciliation record is unknown"));
    }
    if (queue_.size() >= queue_policy_.max_projection_requests)
    {
        return foundation::Result<void>::Failure(
            E("gameplay.runtime_bridge.queue_full", "runtime projection queue capacity is exhausted"));
    }
    queue_.push_back(Queued{found->sequence, found->priority, found->request, 0, std::nullopt});
    reconciliation_.erase(found);
    return foundation::Result<void>::Success();
}

foundation::Result<void> RuntimeBridgeService::DiscardReconciliation(std::uint64_t sequence)
{
    const auto found = std::find_if(reconciliation_.begin(), reconciliation_.end(),
                                    [sequence](const auto& record) { return record.sequence == sequence; });
    if (found == reconciliation_.end())
    {
        return foundation::Result<void>::Failure(
            E("gameplay.runtime_bridge.reconciliation_missing", "runtime projection reconciliation record is unknown"));
    }
    reconciliation_.erase(found);
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<GameplayObjectRef>> RuntimeBridgeService::ReconcileBindings()
{
    std::vector<GameplayObjectRef> ids;
    ids.reserve(bindings_.size());
    for (const auto& [object, _] : bindings_) ids.push_back(object);
    std::sort(ids.begin(), ids.end());

    std::vector<GameplayObjectRef> stale;
    for (const auto object : ids)
    {
        const auto found = bindings_.find(object);
        if (found == bindings_.end()) continue;
        const auto alive = backend_.IsRepresentationAlive(found->second.runtime_object);
        if (!alive)
        {
            return foundation::Result<std::vector<GameplayObjectRef>>::Failure(alive.GetError());
        }
        if (!alive.Value()) stale.push_back(object);
    }
    for (const auto object : stale) RemoveBinding(object);
    return foundation::Result<std::vector<GameplayObjectRef>>::Success(std::move(stale));
}

RuntimeBridgeDiagnostics RuntimeBridgeService::GetDiagnostics() const noexcept
{
    return RuntimeBridgeDiagnostics{bindings_.size(),
                                    projection_requests_,
                                    projection_failures_,
                                    queue_.size(),
                                    observations_,
                                    stale_observations_,
                                    materializations_,
                                    dematerializations_,
                                    coalesced_projection_requests_,
                                    rejected_projection_requests_,
                                    dropped_contacts_,
                                    contact_backlog_.size(),
                                    reconciliation_.size(),
                                    invalid_runtime_observations_};
}

} // namespace epidemic::gameplay::runtime_bridge
