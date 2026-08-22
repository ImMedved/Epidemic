#include "Epidemic/GameFramework/RuntimeBridge/runtime_bridge.h"

#include <cstdlib>
#include <unordered_set>
#include <vector>

#define CHECK(expr)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
            std::abort();                                                                                              \
    } while (false)

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::runtime_bridge;

namespace
{
struct Backend final : IRuntimeBridgeBackend
{
    std::uint64_t next_object = 10;
    int materialize_calls = 0;
    int dematerialize_calls = 0;
    int env_calls = 0;
    bool force_collision_object = false;
    RuntimeObjectHandle collision_object{};
    std::vector<RuntimeContactObservation> contacts;
    std::vector<RuntimeRayHit> ray_hits;
    std::vector<RuntimePhysicsBodyHandle> overlap_hits;
    RuntimePhysicsBodyHandle last_impulse_body{};
    std::unordered_set<std::uint64_t> dead_objects;

    foundation::Result<RuntimeMaterializationResult> Materialize(RuntimePersistentObjectHandle) override
    {
        ++materialize_calls;
        if (force_collision_object)
        {
            return foundation::Result<RuntimeMaterializationResult>::Success({collision_object, true});
        }
        return foundation::Result<RuntimeMaterializationResult>::Success({RuntimeObjectHandle{next_object++}, true});
    }
    foundation::Result<void> Dematerialize(RuntimeObjectHandle) override
    {
        ++dematerialize_calls;
        return foundation::Result<void>::Success();
    }
    foundation::Result<void> Destroy(RuntimeObjectHandle) override { return foundation::Result<void>::Success(); }
    foundation::Result<void> ApplyImpulse(RuntimePhysicsBodyHandle body, RuntimeVector3) override
    {
        last_impulse_body = body;
        return foundation::Result<void>::Success();
    }
    foundation::Result<void> ProjectEnvironment(RuntimeRegionHandle, const RuntimeEnvironmentValues&, Revision) override
    {
        ++env_calls;
        return foundation::Result<void>::Success();
    }
    std::vector<RuntimeContactObservation> ConsumeContacts() override
    {
        auto result = contacts;
        contacts.clear();
        return result;
    }
    foundation::Result<std::vector<RuntimeRayHit>> Raycast(const RuntimeRayQuery&) const override
    {
        return foundation::Result<std::vector<RuntimeRayHit>>::Success(ray_hits);
    }
    foundation::Result<std::vector<RuntimePhysicsBodyHandle>> Overlap(const RuntimeOverlapQuery&) const override
    {
        return foundation::Result<std::vector<RuntimePhysicsBodyHandle>>::Success(overlap_hits);
    }
    foundation::Result<bool> Visible(RuntimeObjectHandle left, RuntimeObjectHandle right) const override
    {
        return foundation::Result<bool>::Success(left.IsValid() && right.IsValid());
    }
    foundation::Result<RuntimeTransformObservation> ObserveTransform(RuntimeObjectHandle object) const override
    {
        if (!object.IsValid() || dead_objects.contains(object.value))
        {
            return foundation::Result<RuntimeTransformObservation>::Failure(
                foundation::Error::Create("test.transform.missing", "runtime object is missing"));
        }
        RuntimeTransformObservation observed;
        observed.position = {1250, 2000, 3000};
        observed.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        observed.scale = {1.0f, 1.0f, 1.0f};
        observed.region = RuntimeRegionHandle{1};
        return foundation::Result<RuntimeTransformObservation>::Success(observed);
    }
    foundation::Result<bool> IsRepresentationAlive(RuntimeObjectHandle object) const override
    {
        return foundation::Result<bool>::Success(object.IsValid() && !dead_objects.contains(object.value));
    }
    foundation::Result<RuntimeNavigationQueryHandle> RequestPath(const RuntimeNavigationPathQuery&) override
    {
        return foundation::Result<RuntimeNavigationQueryHandle>::Success(RuntimeNavigationQueryHandle{91});
    }
    foundation::Result<RuntimeNavigationPathState> GetPathState(RuntimeNavigationQueryHandle handle) const override
    {
        return handle.value == 91
                   ? foundation::Result<RuntimeNavigationPathState>::Success(RuntimeNavigationPathState::Completed)
                   : foundation::Result<RuntimeNavigationPathState>::Failure(
                         foundation::Error::Create("test.navigation.unknown", "unknown navigation handle"));
    }
    foundation::Result<RuntimeNavigationPath> GetPathResult(RuntimeNavigationQueryHandle handle) const override
    {
        if (handle.value != 91)
        {
            return foundation::Result<RuntimeNavigationPath>::Failure(
                foundation::Error::Create("test.navigation.unknown", "unknown navigation handle"));
        }
        RuntimeNavigationPath path;
        path.state = RuntimeNavigationPathState::Completed;
        path.points = {{0, 0, 0}, {1000, 0, 0}};
        path.navigation_revision = 7;
        path.result_revision = 8;
        return foundation::Result<RuntimeNavigationPath>::Success(std::move(path));
    }
    foundation::Result<void> CancelPath(RuntimeNavigationQueryHandle) override { return foundation::Result<void>::Success(); }
    foundation::Result<void> ReleasePath(RuntimeNavigationQueryHandle) override { return foundation::Result<void>::Success(); }
    foundation::Result<RuntimeEnvironmentSample> SampleEnvironment(RuntimeRegionHandle region) const override
    {
        if (!region.IsValid())
        {
            return foundation::Result<RuntimeEnvironmentSample>::Failure(
                foundation::Error::Create("test.environment.invalid", "invalid region"));
        }
        RuntimeEnvironmentSample sample;
        sample.values.temperature_milli_c = 12500;
        sample.revision = Revision{3};
        return foundation::Result<RuntimeEnvironmentSample>::Success(sample);
    }
};

GameplayObjectRef Object(std::string_view name)
{
    const auto domain = GameplayDomainId::FromString("framework.test.runtime_bridge");
    return GameplayObjectRef{domain, GameplayObjectId::FromString(name)};
}
} // namespace

int main()
{
    Backend backend;
    RuntimeBridgeService bridge(backend);
    const auto object = Object("object.a");
    const auto other = Object("object.b");

    CHECK(!bridge.Enqueue(DematerializeRequest{}));
    CHECK(!bridge.Enqueue(DestroyRuntimeRequest{}));
    CHECK(!bridge.Enqueue(ImpulseProjectionRequest{}));
    CHECK(!bridge.Enqueue(EnvironmentProjectionRequest{}));

    WorldAlterationProjectionRequest unsupported;
    unsupported.alteration = GameplayObjectId::FromString("alteration.test");
    unsupported.type = TypeId::FromString("alteration.test");
    unsupported.area = {{0, 0, 0}, {1, 1, 1}};
    CHECK(!bridge.Enqueue(unsupported));

    MaterializeRequest materialize;
    materialize.object = object;
    materialize.persistent_id = RuntimePersistentObjectHandle{42};
    materialize.gameplay_revision = Revision{1};
    CHECK(bridge.Enqueue(materialize));
    auto processed = bridge.Process();
    CHECK(processed.materialized == 1);
    const auto binding = bridge.GetBinding(object);
    CHECK(binding && binding->generation.IsValid() && binding->runtime_object.IsValid());
    const auto generation = binding->generation;
    const auto runtime_object = binding->runtime_object;

    // Re-projecting the same persistent representation only advances the gameplay revision.
    materialize.gameplay_revision = Revision{2};
    CHECK(bridge.Enqueue(materialize));
    CHECK(bridge.Process().failed == 0);
    CHECK(backend.materialize_calls == 1);
    const auto refreshed = bridge.GetBinding(object);
    CHECK(refreshed && refreshed->runtime_object == runtime_object && refreshed->gameplay_revision == Revision{2});
    const auto transform = bridge.ObserveTransform(object);
    CHECK(transform && transform.Value().position.x_mm == 1250 && transform.Value().generation == generation);

    const RuntimePhysicsBodyHandle body{77};
    const GameplayObjectPartRef part{object, TypeId::FromString("test.object.part")};
    CHECK(part.IsValid());
    CHECK(bridge.AttachPhysicsBody(object, generation, body, part));
    CHECK(!bridge.AttachPhysicsBody(object, generation, RuntimePhysicsBodyHandle{78},
                                    GameplayObjectPartRef{other, TypeId::FromString("test.other.part")}));
    ImpulseProjectionRequest impulse;
    impulse.object = object;
    impulse.part = part;
    impulse.generation = generation;
    impulse.impulse = {1.0f, 0.0f, 0.0f};
    CHECK(bridge.Enqueue(impulse));
    CHECK(bridge.Process().failed == 0);
    CHECK(backend.last_impulse_body == body);
    backend.contacts.push_back({body, {}, RuntimeVector3{1.25f, 2.0f, 3.0f}, 4.5f});
    auto observations = bridge.CollectImpactObservations(GameplayTickId{7});
    CHECK(observations.size() == 1);
    CHECK(observations[0].subject == object);
    CHECK(observations[0].subject_part == part);
    CHECK(observations[0].point.x_mm == 1250);
    CHECK(observations[0].impulse_milli == 4500);

    backend.contacts.push_back({body, {}, RuntimeVector3{2.0f, 0.0f, 0.0f}, 1.0f});
    backend.contacts.push_back({body, {}, RuntimeVector3{3.0f, 0.0f, 0.0f}, 2.0f});
    RuntimeBridgeBudget observation_budget;
    observation_budget.max_observations = 1;
    const auto first_budgeted = bridge.CollectImpactObservations(GameplayTickId{8}, observation_budget);
    const auto second_budgeted = bridge.CollectImpactObservations(GameplayTickId{9}, observation_budget);
    CHECK(first_budgeted.size() == 1 && second_budgeted.size() == 1);
    CHECK(first_budgeted[0].point.x_mm == 2000 && second_budgeted[0].point.x_mm == 3000);

    EnvironmentProjectionRequest environment;
    environment.region = RuntimeRegionHandle{1};
    environment.gameplay_revision = Revision{5};
    CHECK(bridge.Enqueue(environment));
    CHECK(bridge.Enqueue(environment));
    CHECK(bridge.Process().processed == 2);
    CHECK(backend.env_calls == 1);

    backend.ray_hits = {{body, RuntimeVector3{2.0f, 0.0f, 0.0f}, RuntimeVector3{-1.0f, 0.0f, 0.0f}, 2.0f}};
    const auto ray = bridge.Raycast(RuntimeRayQuery{});
    CHECK(ray && ray.Value().size() == 1 && ray.Value()[0].object == object && ray.Value()[0].part == part &&
          ray.Value()[0].point.x_mm == 2000);
    backend.overlap_hits = {body};
    const auto overlap = bridge.Overlap(RuntimeOverlapQuery{{{0, 0, 0}, {1, 1, 1}}});
    CHECK(overlap && overlap.Value().size() == 1 && overlap.Value()[0].object == object && overlap.Value()[0].part == part);

    const auto path_handle = bridge.RequestPath(RuntimeNavigationPathQuery{{0, 0, 0}, {1000, 0, 0}, RuntimeRegionHandle{1}, 6});
    CHECK(path_handle && path_handle.Value().IsValid());
    const auto path_state = bridge.GetPathState(path_handle.Value());
    CHECK(path_state && path_state.Value() == RuntimeNavigationPathState::Completed);
    const auto path_result = bridge.GetPathResult(path_handle.Value());
    CHECK(path_result && path_result.Value().IsComplete() && path_result.Value().points.size() == 2);
    CHECK(bridge.CancelPath(path_handle.Value()) && bridge.ReleasePath(path_handle.Value()));
    const auto environment_sample = bridge.SampleEnvironment(RuntimeRegionHandle{1});
    CHECK(environment_sample && environment_sample.Value().values.temperature_milli_c == 12500 &&
          environment_sample.Value().revision == Revision{3});

    // A backend collision after creating a representation is rolled back instead of orphaning it.
    MaterializeRequest other_materialize;
    other_materialize.object = other;
    other_materialize.persistent_id = RuntimePersistentObjectHandle{43};
    other_materialize.gameplay_revision = Revision{1};
    backend.force_collision_object = true;
    backend.collision_object = runtime_object;
    const auto dematerialize_before_collision = backend.dematerialize_calls;
    CHECK(bridge.Enqueue(other_materialize));
    const auto collision_result = bridge.Process();
    CHECK(collision_result.failed == 1);
    CHECK(!bridge.GetBinding(other).has_value());
    CHECK(backend.dematerialize_calls == dematerialize_before_collision + 1);
    backend.force_collision_object = false;

    const auto vanished = Object("object.vanished");
    MaterializeRequest vanished_materialize;
    vanished_materialize.object = vanished;
    vanished_materialize.persistent_id = RuntimePersistentObjectHandle{44};
    vanished_materialize.gameplay_revision = Revision{1};
    CHECK(bridge.Enqueue(vanished_materialize));
    CHECK(bridge.Process().materialized == 1);
    const auto vanished_binding = bridge.GetBinding(vanished);
    CHECK(vanished_binding);
    backend.dead_objects.insert(vanished_binding->runtime_object.value);
    const auto reconciled = bridge.ReconcileBindings();
    CHECK(reconciled && reconciled.Value().size() == 1 && reconciled.Value().front() == vanished);
    CHECK(!bridge.GetBinding(vanished));

    DematerializeRequest dematerialize;
    dematerialize.object = object;
    dematerialize.generation = generation;
    CHECK(bridge.Enqueue(dematerialize));
    const auto dematerialized = bridge.Process();
    CHECK(dematerialized.dematerialized == 1);
    CHECK(!bridge.GetBinding(object).has_value());
    return 0;
}
