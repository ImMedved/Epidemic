#include "Epidemic/GameFramework/Conditions/conditions.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::conditions;

namespace
{
struct Payload
{
    int value = 0;
};

class TestSubjectStateProvider final : public IConditionSubjectStateProvider
{
  public:
    ConditionSubjectMaterializationState state = ConditionSubjectMaterializationState::Materialized;
    bool throw_on_query = false;

    [[nodiscard]] ConditionSubjectMaterializationState GetMaterializationState(GameplayObjectRef) const override
    {
        if (throw_on_query)
        {
            throw std::runtime_error("subject state provider failure");
        }
        return state;
    }
};
}

int main()
{
    GameplayTagRegistry tags;
    const auto harmful = tags.Register("condition.harmful");
    if (!harmful) return 1;
    tags.Freeze();

    ConditionService service;
    TestSubjectStateProvider subject_state_provider;
    if (!service.SetSubjectStateProvider(&subject_state_provider)) return 201;
    const auto clock = ClockId::FromString("test.clock");
    const auto payload_type = TypeId::FromString("test.condition.payload");

    ConditionDefinition stacking;
    stacking.canonical_name = "test.condition.stacking";
    stacking.tags.Add(harmful.Value());
    stacking.stacking = ConditionStackingPolicy::AddStacks;
    stacking.max_stacks = 3;
    stacking.default_duration = GameplayDuration{10};
    stacking.periodic_interval = GameplayDuration{2};
    stacking.clock = clock;
    stacking.persistence = ConditionPersistencePolicy::Persistent;
    stacking.payload_type = payload_type;
    stacking.max_payload_bytes = sizeof(Payload);
    const auto stacking_id = service.RegisterCondition(stacking, [](std::span<const std::byte> bytes) { return bytes.size() == sizeof(Payload); });

    ConditionDefinition refresh;
    refresh.canonical_name = "test.condition.refresh";
    refresh.stacking = ConditionStackingPolicy::RefreshDuration;
    refresh.default_duration = GameplayDuration{5};
    refresh.clock = clock;
    const auto refresh_id = service.RegisterCondition(refresh);

    ConditionDefinition stronger;
    stronger.canonical_name = "test.condition.stronger";
    stronger.stacking = ConditionStackingPolicy::ReplaceIfStronger;
    stronger.clock = clock;
    const auto stronger_id = service.RegisterCondition(stronger);

    ConditionDefinition materialized;
    materialized.canonical_name = "test.condition.materialized";
    materialized.stacking = ConditionStackingPolicy::UniquePerSubject;
    materialized.materialization = ConditionMaterializationPolicy::MaterializedOnly;
    materialized.dematerialization = ConditionDematerializationPolicy::Pause;
    materialized.default_duration = GameplayDuration{10};
    materialized.clock = clock;
    const auto materialized_id = service.RegisterCondition(materialized);

    ConditionDefinition throwing_payload;
    throwing_payload.canonical_name = "test.condition.throwing_payload";
    throwing_payload.payload_type = TypeId::FromString("test.condition.throwing_payload.data");
    throwing_payload.max_payload_bytes = sizeof(Payload);
    const auto throwing_payload_id = service.RegisterCondition(throwing_payload, [](std::span<const std::byte>) -> bool {
        throw std::runtime_error("validator failure");
    });

    if (!stacking_id || !refresh_id || !stronger_id || !materialized_id || !throwing_payload_id) return 2;
    service.Freeze();
    if (service.SetSubjectStateProvider(&subject_state_provider)) return 202;
    ConditionDefinition late_condition;
    late_condition.canonical_name = "test.condition.late";
    if (service.RegisterCondition(late_condition)) return 3;

    const GameplayObjectRef subject{GameplayDomainId::FromString("test.entities"), GameplayObjectId::FromString("subject")};
    GameplayContext context;
    context.tick = GameplayTickId{1};
    context.time = GameplayTimePoint{100};

    ApplyConditionRequest request;
    request.type = stacking_id.Value();
    request.subject = subject;
    request.magnitude_micro = 10;
    request.payload = RegisteredConditionPayload::FromTrivial(payload_type, Payload{7});
    request.context = context;
    const auto a = service.Apply(request);
    const auto b = service.Apply(request);
    const auto c = service.Apply(request);
    const auto d = service.Apply(request);
    if (!a || !b || !c || !d || a.Value().disposition != ConditionApplyDisposition::Added ||
        b.Value().disposition != ConditionApplyDisposition::Stacked || c.Value().disposition != ConditionApplyDisposition::Stacked ||
        d.Value().disposition != ConditionApplyDisposition::NoOp) return 4;
    const auto* stacked = service.Find(a.Value().instance);
    if (stacked == nullptr || stacked->stacks != 3 || !stacked->expires_at.has_value() || stacked->expires_at->ticks != 110) return 5;

    ApplyConditionRequest bad_payload = request;
    bad_payload.payload.bytes.clear();
    if (service.Apply(bad_payload)) return 6;

    ApplyConditionRequest throwing_request;
    throwing_request.type = throwing_payload_id.Value();
    throwing_request.subject = subject;
    throwing_request.payload = RegisteredConditionPayload::FromTrivial(TypeId::FromString("test.condition.throwing_payload.data"), Payload{1});
    throwing_request.context = context;
    const auto before_throwing_validator = service.AllConditions().size();
    if (service.Apply(throwing_request) || service.AllConditions().size() != before_throwing_validator) return 206;

    ApplyConditionRequest refresh_request;
    refresh_request.type = refresh_id.Value();
    refresh_request.subject = subject;
    refresh_request.context = context;
    const auto r1 = service.Apply(refresh_request);
    context.time = GameplayTimePoint{103};
    refresh_request.context = context;
    const auto r2 = service.Apply(refresh_request);
    if (!r1 || !r2 || r2.Value().disposition != ConditionApplyDisposition::Refreshed ||
        service.Find(r1.Value().instance)->expires_at->ticks != 108) return 7;

    ApplyConditionRequest strong;
    strong.type = stronger_id.Value();
    strong.subject = subject;
    strong.context = context;
    strong.magnitude_micro = 10;
    const auto s1 = service.Apply(strong);
    strong.magnitude_micro = 5;
    const auto s2 = service.Apply(strong);
    strong.magnitude_micro = 20;
    const auto s3 = service.Apply(strong);
    if (!s1 || !s2 || !s3 || s2.Value().disposition != ConditionApplyDisposition::NoOp ||
        s3.Value().disposition != ConditionApplyDisposition::Replaced) return 8;

    ApplyConditionRequest mat;
    mat.type = materialized_id.Value();
    mat.subject = subject;
    mat.context = context;

    subject_state_provider.state = ConditionSubjectMaterializationState::Abstract;
    if (service.Apply(mat)) return 203;
    subject_state_provider.state = ConditionSubjectMaterializationState::Unavailable;
    if (service.Apply(mat)) return 204;
    subject_state_provider.state = ConditionSubjectMaterializationState::Materialized;
    subject_state_provider.throw_on_query = true;
    if (service.Apply(mat)) return 205;
    subject_state_provider.throw_on_query = false;

    const auto m = service.Apply(mat);
    GameplayContext pause_context = context;
    pause_context.time = GameplayTimePoint{104};
    if (!m || !service.NotifySubjectMaterialization(subject, false, pause_context)) return 9;
    if (!service.Find(m.Value().instance)->paused_for_materialization) return 10;
    // Expiration callbacks while paused are ignored and must not remove the condition.
    if (!service.HandleExpirationDue(m.Value().instance, GameplayTimePoint{114}, pause_context) ||
        service.Find(m.Value().instance) == nullptr) return 101;
    GameplayContext resume_context = context;
    resume_context.time = GameplayTimePoint{120};
    if (!service.NotifySubjectMaterialization(subject, true, resume_context) || service.Find(m.Value().instance)->paused_for_materialization) return 11;
    const auto* resumed = service.Find(m.Value().instance);
    // Applied at t=103, original expiration t=113, paused from 104..120 (16 ticks):
    // both phase and expiration move by exactly 16 ticks.
    if (resumed == nullptr || resumed->applied_at.ticks != 119 || !resumed->expires_at.has_value() || resumed->expires_at->ticks != 129) return 111;
    if (service.HandleExpirationDue(m.Value().instance, GameplayTimePoint{128}, resume_context)) return 112;
    if (!service.HandleExpirationDue(m.Value().instance, GameplayTimePoint{129}, resume_context)) return 113;

    if (!service.SetScheduleLinks(a.Value().instance, ScheduleId::FromString("expire"), ScheduleId::FromString("periodic"))) return 12;
    if (!service.HandlePeriodicDue(a.Value().instance, 7, context)) return 13;
    const auto early_expiration = service.HandleExpirationDue(a.Value().instance, GameplayTimePoint{109}, context);
    if (early_expiration || service.Find(a.Value().instance) == nullptr) return 14;
    if (!service.HandleExpirationDue(a.Value().instance, GameplayTimePoint{110}, context)) return 15;
    if (service.Find(a.Value().instance) != nullptr) return 16;

    const auto harmful_conditions = service.RemoveByTag(subject, harmful.Value(), tags, ConditionRemovalReason::Dispelled, context);
    if (harmful_conditions != 0) return 17; // stacking condition already expired; others have no harmful tag.

    ApplyConditionRequest persistent_again = request;
    persistent_again.context = context;
    persistent_again.context.time = GameplayTimePoint{130};
    if (!service.Apply(persistent_again)) return 211;

    const auto snapshot = service.CaptureSnapshot();
    const auto count = snapshot.instances.size();
    const auto current_count_before_invalid_restore = service.AllConditions().size();

    auto bad_scope_snapshot = snapshot;
    ++bad_scope_snapshot.id_generator.scope;
    if (service.RestoreSnapshot(bad_scope_snapshot) || service.AllConditions().size() != current_count_before_invalid_restore) return 207;

    auto bad_next_snapshot = snapshot;
    bad_next_snapshot.id_generator.next = 1;
    if (service.RestoreSnapshot(bad_next_snapshot) || service.AllConditions().size() != current_count_before_invalid_restore) return 208;

    if (!service.RemoveSubject(subject, ConditionRemovalReason::SystemCleanup, context) || !service.AllConditions().empty()) return 18;
    if (!service.RestoreSnapshot(snapshot) || service.AllConditions().size() != count) return 19;

    const auto diagnostics = service.GetDiagnostics();
    if (diagnostics.active_conditions != count) return 20;

    // Materialization resume rejects an unrepresentable pause interval without mutating the condition.
    subject_state_provider.state = ConditionSubjectMaterializationState::Materialized;
    ApplyConditionRequest overflow_pause = mat;
    overflow_pause.context.time = GameplayTimePoint{std::numeric_limits<std::int64_t>::min()};
    auto overflow_instance = service.Apply(overflow_pause);
    if (!overflow_instance) return 212;
    GameplayContext overflow_pause_context = overflow_pause.context;
    if (!service.NotifySubjectMaterialization(subject, false, overflow_pause_context)) return 213;
    const auto *paused_before = service.Find(overflow_instance.Value().instance);
    if (paused_before == nullptr || !paused_before->paused_for_materialization || !paused_before->materialization_paused_at) return 214;
    const auto before_applied = paused_before->applied_at;
    const auto before_expires = paused_before->expires_at;
    const auto before_paused_at = paused_before->materialization_paused_at;
    const auto before_revision = paused_before->revision;
    GameplayContext overflow_resume_context = context;
    overflow_resume_context.time = GameplayTimePoint{std::numeric_limits<std::int64_t>::max()};
    auto overflow_resume = service.NotifySubjectMaterialization(subject, true, overflow_resume_context);
    if (overflow_resume || !overflow_resume.GetError().HasCode("gameplay.time_overflow")) return 215;
    const auto *paused_after = service.Find(overflow_instance.Value().instance);
    if (paused_after == nullptr || !paused_after->paused_for_materialization ||
        paused_after->applied_at != before_applied || paused_after->expires_at != before_expires ||
        paused_after->materialization_paused_at != before_paused_at || paused_after->revision != before_revision) return 216;

    ConditionService no_provider_service;
    ConditionDefinition no_provider_materialized = materialized;
    no_provider_materialized.canonical_name = "test.condition.materialized.no_provider";
    no_provider_materialized.id = {};
    const auto no_provider_id = no_provider_service.RegisterCondition(no_provider_materialized);
    if (!no_provider_id) return 209;
    no_provider_service.Freeze();
    ApplyConditionRequest no_provider_request;
    no_provider_request.type = no_provider_id.Value();
    no_provider_request.subject = subject;
    no_provider_request.context = context;
    if (no_provider_service.Apply(no_provider_request)) return 210;

    // COND-01: expiration overflow is rejected before consuming a condition ID.
    const auto generator_before_overflow = service.CaptureSnapshot().id_generator;
    ApplyConditionRequest time_overflow_request;
    time_overflow_request.type = refresh_id.Value();
    time_overflow_request.subject = {GameplayDomainId::FromString("test"), GameplayObjectId::FromString("overflow-new")};
    time_overflow_request.context = context;
    time_overflow_request.context.time = GameplayTimePoint{std::numeric_limits<std::int64_t>::max() - 2};
    auto time_overflow_result = service.Apply(time_overflow_request);
    const auto generator_after_overflow = service.CaptureSnapshot().id_generator;
    if (time_overflow_result || !time_overflow_result.GetError().HasCode("gameplay.time_overflow") ||
        generator_before_overflow.scope != generator_after_overflow.scope ||
        generator_before_overflow.next != generator_after_overflow.next)
        return 217;

    // COND-05: an engaged optional containing an invalid ScheduleId is caller-invalid input.
    const auto current_conditions = service.AllConditions();
    if (current_conditions.empty()) return 218;
    const auto link_target = current_conditions.front().id;
    const auto link_before = service.FindCopy(link_target);
    const std::optional<ScheduleId> invalid_schedule{ScheduleId{}};
    auto invalid_links = service.SetScheduleLinks(link_target, invalid_schedule, std::nullopt, context);
    const auto link_after = service.FindCopy(link_target);
    if (invalid_links || !link_before || !link_after || link_before->expiration_schedule != link_after->expiration_schedule ||
        link_before->periodic_schedule != link_after->periodic_schedule || link_before->revision != link_after->revision)
        return 219;

    // COND-06: batch materialization is preflighted as a whole. A late overflow leaves earlier conditions unchanged.
    ConditionService batch_service;
    TestSubjectStateProvider batch_provider;
    if (!batch_service.SetSubjectStateProvider(&batch_provider)) return 220;
    ConditionDefinition batch_definition;
    batch_definition.canonical_name = "test.condition.batch_pause";
    batch_definition.clock = clock;
    batch_definition.persistence = ConditionPersistencePolicy::Persistent;
    batch_definition.materialization = ConditionMaterializationPolicy::MaterializedOnly;
    batch_definition.dematerialization = ConditionDematerializationPolicy::Pause;
    batch_definition.stacking = ConditionStackingPolicy::Independent;
    auto batch_type = batch_service.RegisterCondition(batch_definition);
    if (!batch_type) return 221;
    batch_service.Freeze();
    const GameplayObjectRef batch_subject{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("batch-subject")};
    ApplyConditionRequest batch_request;
    batch_request.type = batch_type.Value();
    batch_request.subject = batch_subject;
    batch_request.context.time = GameplayTimePoint{0};
    auto batch_a = batch_service.Apply(batch_request);
    auto batch_b = batch_service.Apply(batch_request);
    if (!batch_a || !batch_b) return 222;
    GameplayContext batch_pause_context;
    batch_pause_context.time = GameplayTimePoint{0};
    if (!batch_service.NotifySubjectMaterialization(batch_subject, false, batch_pause_context)) return 223;
    auto batch_snapshot = batch_service.CaptureSnapshot();
    if (batch_snapshot.instances.size() != 2) return 224;
    std::sort(batch_snapshot.instances.begin(), batch_snapshot.instances.end(), [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    batch_snapshot.instances[0].materialization_paused_at = GameplayTimePoint{std::numeric_limits<std::int64_t>::max() - 10};
    batch_snapshot.instances[1].materialization_paused_at = GameplayTimePoint{std::numeric_limits<std::int64_t>::min()};
    if (!batch_service.RestoreSnapshot(batch_snapshot)) return 225;
    const auto batch_before = batch_service.AllConditions();
    GameplayContext batch_resume_context;
    batch_resume_context.time = GameplayTimePoint{std::numeric_limits<std::int64_t>::max()};
    auto batch_resume = batch_service.NotifySubjectMaterialization(batch_subject, true, batch_resume_context);
    const auto batch_after = batch_service.AllConditions();
    if (batch_resume || !batch_resume.GetError().HasCode("gameplay.time_overflow") || batch_before.size() != batch_after.size())
        return 226;
    for (std::size_t i = 0; i < batch_before.size(); ++i)
    {
        if (batch_before[i].id != batch_after[i].id || batch_before[i].applied_at != batch_after[i].applied_at ||
            batch_before[i].expires_at != batch_after[i].expires_at ||
            batch_before[i].materialization_paused_at != batch_after[i].materialization_paused_at ||
            batch_before[i].paused_for_materialization != batch_after[i].paused_for_materialization ||
            batch_before[i].revision != batch_after[i].revision)
            return 227;
    }

    // COND-04: global revision exhaustion rejects a valid mutation before ID/state publication.
    auto exhausted_condition_snapshot = service.CaptureSnapshot();
    exhausted_condition_snapshot.revision = Revision{std::numeric_limits<std::uint64_t>::max()};
    if (!service.RestoreSnapshot(exhausted_condition_snapshot)) return 228;
    const auto exhausted_generator_before = service.CaptureSnapshot().id_generator;
    ApplyConditionRequest exhausted_apply;
    exhausted_apply.type = refresh_id.Value();
    exhausted_apply.subject = {GameplayDomainId::FromString("test"), GameplayObjectId::FromString("revision-exhausted")};
    exhausted_apply.context.time = GameplayTimePoint{200};
    auto exhausted_result = service.Apply(exhausted_apply);
    const auto exhausted_generator_after = service.CaptureSnapshot().id_generator;
    if (exhausted_result || !exhausted_result.GetError().HasCode("gameplay.revision_exhausted") ||
        exhausted_generator_before.next != exhausted_generator_after.next)
        return 229;

    return 0;
}

