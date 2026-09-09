#include "Epidemic/GameFramework/RuntimeBridge/runtime_bridge.h"

#include <cstdlib>
#include <limits>
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
    int env_successes = 0;
    int environment_failures_remaining = 0;
    bool environment_always_fails = false;
    bool force_collision_object = false;
    RuntimeObjectHandle collision_object{};
    std::vector<RuntimeContactObservation> contacts;
    std::vector<RuntimeRayHit> ray_hits;
    std::vector<RuntimePhysicsBodyHandle> overlap_hits;
    RuntimePhysicsBodyHandle last_impulse_body{};
    std::unordered_set<std::uint64_t> dead_objects;
    RuntimeBridgeCapabilities capabilities{};

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
        if (environment_always_fails || environment_failures_remaining > 0)
        {
            if (environment_failures_remaining > 0) --environment_failures_remaining;
            return foundation::Result<void>::Failure(
                foundation::Error::Create("test.environment.transient", "simulated runtime projection failure"));
        }
        ++env_successes;
        return foundation::Result<void>::Success();
    }
    RuntimeBridgeCapabilities GetCapabilities() const noexcept override { return capabilities; }
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
    RuntimeBridgeQueuePolicy main_policy;
    main_policy.max_projection_attempts = 1;
    RuntimeBridgeService bridge(backend, main_policy);
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

    const auto invalid_before = bridge.GetDiagnostics().invalid_runtime_observations;
    backend.contacts.push_back({body, {}, RuntimeVector3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, 1.0f});
    backend.contacts.push_back({body, {}, RuntimeVector3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, 1.0f});
    backend.contacts.push_back({body, {}, RuntimeVector3{-std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, 1.0f});
    backend.contacts.push_back({body, {}, RuntimeVector3{std::numeric_limits<float>::max(), 0.0f, 0.0f}, 1.0f});
    backend.contacts.push_back({body, {}, RuntimeVector3{1.5f, 0.0f, 0.0f}, 2.25f});
    const auto mixed_observations = bridge.CollectImpactObservations(GameplayTickId{71});
    CHECK(mixed_observations.size() == 1);
    CHECK(mixed_observations.front().point.x_mm == 1500 && mixed_observations.front().impulse_milli == 2250);
    CHECK(bridge.GetDiagnostics().invalid_runtime_observations == invalid_before + 4);

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
    environment.gameplay_revision = Revision{6};
    CHECK(bridge.Enqueue(environment));
    CHECK(bridge.GetDiagnostics().coalesced_projection_requests == 1);
    CHECK(bridge.Process().processed == 1);
    CHECK(backend.env_calls == 1);

    backend.capabilities.environment_visibility = false;
    EnvironmentProjectionRequest unsupported_environment = environment;
    unsupported_environment.values.visibility_milli = 500;
    CHECK(!bridge.Enqueue(unsupported_environment));
    backend.capabilities.environment_visibility = true;

    backend.ray_hits = {{body, RuntimeVector3{2.0f, 0.0f, 0.0f}, RuntimeVector3{-1.0f, 0.0f, 0.0f}, 2.0f}};
    const auto ray = bridge.Raycast(RuntimeRayQuery{});
    CHECK(ray && ray.Value().size() == 1 && ray.Value()[0].object == object && ray.Value()[0].part == part &&
          ray.Value()[0].point.x_mm == 2000);
    backend.ray_hits = {{body, RuntimeVector3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
                         RuntimeVector3{-1.0f, 0.0f, 0.0f}, 2.0f}};
    const auto invalid_ray_point = bridge.Raycast(RuntimeRayQuery{});
    CHECK(!invalid_ray_point && invalid_ray_point.GetError().HasCode("gameplay.runtime_bridge.invalid_runtime_observation"));
    backend.ray_hits = {{body, RuntimeVector3{2.0f, 0.0f, 0.0f}, RuntimeVector3{-1.0f, 0.0f, 0.0f},
                         std::numeric_limits<float>::infinity()}};
    const auto invalid_ray_distance = bridge.Raycast(RuntimeRayQuery{});
    CHECK(!invalid_ray_distance && invalid_ray_distance.GetError().HasCode("gameplay.runtime_bridge.invalid_runtime_observation"));
    backend.ray_hits = {{body, RuntimeVector3{2.0f, 0.0f, 0.0f}, RuntimeVector3{-1.0f, 0.0f, 0.0f}, 2.0f}};
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
    const auto collision_reconciliation = bridge.ReconciliationRequests();
    CHECK(collision_reconciliation.size() == 1);
    CHECK(!bridge.ForgetObjectIdentity(other));
    CHECK(bridge.DiscardReconciliation(collision_reconciliation.front().sequence));
    CHECK(bridge.ForgetObjectIdentity(other));
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

    materialize.gameplay_revision = Revision{3};
    CHECK(bridge.Enqueue(materialize));
    CHECK(bridge.Process().materialized == 1);
    const auto rematerialized = bridge.GetBinding(object);
    CHECK(rematerialized && rematerialized->generation.value == generation.value + 1);
    CHECK(!bridge.ForgetObjectIdentity(object));
    DematerializeRequest second_dematerialize{object, rematerialized->generation};
    CHECK(bridge.Enqueue(second_dematerialize));
    CHECK(bridge.Process().dematerialized == 1);
    CHECK(bridge.ForgetObjectIdentity(object));
    materialize.gameplay_revision = Revision{4};
    CHECK(bridge.Enqueue(materialize));
    CHECK(bridge.Process().materialized == 1);
    const auto after_forget = bridge.GetBinding(object);
    CHECK(after_forget && after_forget->generation.value == 1);

    // D1-H01: transient backend failure stays retryable and does not block neighboring commands.
    Backend retry_backend;
    retry_backend.environment_failures_remaining = 1;
    RuntimeBridgeQueuePolicy retry_policy;
    retry_policy.max_projection_requests = 8;
    retry_policy.max_projection_attempts = 2;
    retry_policy.max_reconciliation_requests = 2;
    RuntimeBridgeService retry_bridge(retry_backend, retry_policy);
    EnvironmentProjectionRequest retry_environment;
    retry_environment.region = RuntimeRegionHandle{1};
    retry_environment.gameplay_revision = Revision{10};
    MaterializeRequest retry_neighbor{Object("retry.neighbor"), RuntimePersistentObjectHandle{70}, Revision{1}};
    CHECK(retry_bridge.Enqueue(retry_environment));
    CHECK(retry_bridge.Enqueue(retry_neighbor));
    const auto retry_first = retry_bridge.Process();
    CHECK(retry_first.failed == 1 && retry_first.materialized == 1);
    CHECK(retry_bridge.GetDiagnostics().projection_backlog == 1);
    const auto retry_second = retry_bridge.Process();
    CHECK(retry_second.failed == 0 && retry_backend.env_calls == 2 && retry_backend.env_successes == 1);
    CHECK(retry_bridge.GetDiagnostics().projection_backlog == 0);

    // Exhausted retries enter bounded reconciliation storage and may be explicitly retried/discarded.
    Backend dead_backend;
    dead_backend.environment_always_fails = true;
    RuntimeBridgeQueuePolicy dead_policy;
    dead_policy.max_projection_requests = 4;
    dead_policy.max_projection_attempts = 2;
    dead_policy.max_reconciliation_requests = 1;
    RuntimeBridgeService dead_bridge(dead_backend, dead_policy);
    EnvironmentProjectionRequest dead_environment;
    dead_environment.region = RuntimeRegionHandle{2};
    dead_environment.gameplay_revision = Revision{5};
    CHECK(dead_bridge.Enqueue(dead_environment));
    CHECK(dead_bridge.Process().failed == 1);
    CHECK(dead_bridge.Process().failed == 1);
    auto dead_records = dead_bridge.ReconciliationRequests();
    CHECK(dead_records.size() == 1 && dead_records.front().attempts == 2 && dead_records.front().last_error.has_value());
    CHECK(!dead_bridge.ForgetEnvironmentProjection(dead_environment.region));

    // A newer successful revision wins; retrying the older reconciliation record must not project stale state.
    dead_backend.environment_always_fails = false;
    EnvironmentProjectionRequest newer_environment = dead_environment;
    newer_environment.gameplay_revision = Revision{6};
    CHECK(dead_bridge.Enqueue(newer_environment));
    CHECK(dead_bridge.Process().failed == 0 && dead_backend.env_successes == 1);
    const auto stale_sequence = dead_records.front().sequence;
    CHECK(dead_bridge.RetryReconciliation(stale_sequence));
    const auto env_calls_before_stale_retry = dead_backend.env_calls;
    CHECK(dead_bridge.Process().failed == 0);
    CHECK(dead_backend.env_calls == env_calls_before_stale_retry && dead_backend.env_successes == 1);

    // Reconciliation capacity creates backpressure instead of dropping exhausted commands.
    dead_backend.environment_always_fails = true;
    EnvironmentProjectionRequest dead_a = dead_environment;
    dead_a.region = RuntimeRegionHandle{3};
    EnvironmentProjectionRequest dead_b = dead_environment;
    dead_b.region = RuntimeRegionHandle{4};
    CHECK(dead_bridge.Enqueue(dead_a));
    CHECK(dead_bridge.Enqueue(dead_b));
    CHECK(dead_bridge.Process().failed == 2);
    CHECK(dead_bridge.Process().failed == 2);
    CHECK(dead_bridge.ReconciliationRequests().size() == 1);
    CHECK(dead_bridge.GetDiagnostics().projection_backlog == 1);
    const auto first_dead = dead_bridge.ReconciliationRequests().front().sequence;
    CHECK(dead_bridge.DiscardReconciliation(first_dead));
    CHECK(dead_bridge.Process().failed == 0);
    CHECK(dead_bridge.ReconciliationRequests().size() == 1);
    CHECK(dead_bridge.DiscardReconciliation(dead_bridge.ReconciliationRequests().front().sequence));

    Backend limited_backend;
    RuntimeBridgeService limited(limited_backend, RuntimeBridgeQueuePolicy{1, 1});
    MaterializeRequest q1{Object("queue.1"), RuntimePersistentObjectHandle{1}, Revision{1}};
    MaterializeRequest q2{Object("queue.2"), RuntimePersistentObjectHandle{2}, Revision{1}};
    CHECK(limited.Enqueue(q1));
    CHECK(!limited.Enqueue(q2));
    CHECK(limited.GetDiagnostics().rejected_projection_requests == 1);

    limited_backend.contacts.push_back({RuntimePhysicsBodyHandle{1}, {}, RuntimeVector3{1, 0, 0}, 1.0f});
    limited_backend.contacts.push_back({RuntimePhysicsBodyHandle{2}, {}, RuntimeVector3{2, 0, 0}, 1.0f});
    RuntimeBridgeBudget no_observations;
    no_observations.max_observations = 0;
    (void)limited.CollectImpactObservations(GameplayTickId{10}, no_observations);
    const auto limited_diag = limited.GetDiagnostics();
    CHECK(limited_diag.contact_backlog == 1);
    CHECK(limited_diag.dropped_contacts == 1);
    return 0;
}
