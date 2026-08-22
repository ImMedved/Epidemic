#include "Epidemic/GameFramework/Conditions/conditions.h"

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::conditions;

namespace
{
struct Payload
{
    int value = 0;
};
}

int main()
{
    GameplayTagRegistry tags;
    const auto harmful = tags.Register("condition.harmful");
    if (!harmful) return 1;
    tags.Freeze();

    ConditionService service;
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

    if (!stacking_id || !refresh_id || !stronger_id || !materialized_id) return 2;
    service.Freeze();
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

    const auto snapshot = service.CaptureSnapshot();
    const auto count = snapshot.instances.size();
    if (!service.RemoveSubject(subject, ConditionRemovalReason::SystemCleanup, context) || !service.AllConditions().empty()) return 18;
    if (!service.RestoreSnapshot(snapshot) || service.AllConditions().size() != count) return 19;

    const auto diagnostics = service.GetDiagnostics();
    if (diagnostics.active_conditions != count) return 20;
    return 0;
}

