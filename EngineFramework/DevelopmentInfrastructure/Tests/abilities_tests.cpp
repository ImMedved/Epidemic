#include "Epidemic/GameFramework/Abilities/abilities.h"
#include "Epidemic/Foundation/error.h"
#include "../../GameplayWorldStateOwners/Abilities/src/abilities_test_seam.h"

#include <algorithm>
#include <limits>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::abilities;

namespace
{
template <class T, class Pred>
bool SameVector(const std::vector<T>& a, const std::vector<T>& b, Pred pred)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), pred);
}

bool SameTargetSet(const AbilityTargetSet& a, const AbilityTargetSet& b)
{
    return a.primary == b.primary && a.targets == b.targets && a.point_micro == b.point_micro &&
           a.direction_micro == b.direction_micro && a.area_radius_micro == b.area_radius_micro;
}

bool SameReservation(const AbilityResourceReservation& a, const AbilityResourceReservation& b)
{
    return a.id == b.id && a.resource == b.resource && a.provider_token.type == b.provider_token.type &&
           a.provider_token.bytes == b.provider_token.bytes;
}

bool SameInstance(const AbilityInstance& a, const AbilityInstance& b)
{
    return a.id == b.id && a.definition == b.definition && a.owner == b.owner && a.source == b.source &&
           a.persistence == b.persistence && a.enabled == b.enabled && a.revision == b.revision;
}

bool SameExecution(const AbilityExecution& a, const AbilityExecution& b)
{
    return a.id == b.id && a.ability == b.ability && a.owner == b.owner && SameTargetSet(a.targets, b.targets) &&
           a.state == b.state && a.started_at == b.started_at && a.due_at == b.due_at &&
           a.next_channel_at == b.next_channel_at &&
           SameVector(a.reservations, b.reservations, SameReservation) && a.schedule == b.schedule &&
           a.context == b.context && a.revision == b.revision;
}

bool SameCooldown(const AbilityCooldownState& a, const AbilityCooldownState& b)
{
    return a.owner == b.owner && a.group == b.group && a.ends_at == b.ends_at;
}

bool SameChange(const AbilityChange& a, const AbilityChange& b)
{
    return a.sequence == b.sequence && a.kind == b.kind && a.owner == b.owner && a.ability == b.ability &&
           a.execution == b.execution && a.time == b.time && a.context == b.context && a.reason == b.reason;
}

bool SameSnapshot(const AbilitiesSnapshot& a, const AbilitiesSnapshot& b)
{
    return SameVector(a.instances, b.instances, SameInstance) &&
           SameVector(a.executions, b.executions, SameExecution) &&
           SameVector(a.cooldowns, b.cooldowns, SameCooldown) &&
           a.instance_ids.scope == b.instance_ids.scope && a.instance_ids.next == b.instance_ids.next &&
           a.execution_ids.scope == b.execution_ids.scope && a.execution_ids.next == b.execution_ids.next &&
           SameVector(a.journal, b.journal, SameChange) && a.next_change_sequence == b.next_change_sequence &&
           a.change_epoch == b.change_epoch;
}

class Resource final : public IAbilityResourceProvider
{
  public:
    std::int64_t balance = 100;
    AbilityResourceTypeId fail_reserve{};
    std::uint64_t reconciled = 0;
    std::uint64_t reconcile_calls = 0;
    std::uint64_t fail_reconcile_call = 0;
    std::uint64_t reserve_counter = 0;

    [[nodiscard]] bool CanAfford(GameplayObjectRef, AbilityResourceTypeId, std::int64_t amount) const override
    {
        return balance >= amount;
    }

    [[nodiscard]] foundation::Result<AbilityResourceReservation> Reserve(GameplayObjectRef, AbilityResourceTypeId type,
                                                                         std::int64_t amount,
                                                                         GameplayContext) override
    {
        if (type == fail_reserve)
        {
            return foundation::Result<AbilityResourceReservation>::Failure(
                foundation::Error::Create("reserve", "reserve failed"));
        }
        if (balance < amount)
        {
            return foundation::Result<AbilityResourceReservation>::Failure(foundation::Error::Create("no", "no"));
        }
        balance -= amount;
        ++reserve_counter;
        struct Payload
        {
            std::int64_t amount;
        };
        return foundation::Result<AbilityResourceReservation>::Success(
            {{GameplayObjectId::FromRaw(1, reserve_counter)}, type,
             RegisteredAbilityPayload::FromTrivial(TypeId::FromString("p"), Payload{amount})});
    }

    void Commit(const AbilityResourceReservation &, GameplayContext) noexcept override {}

    void Release(const AbilityResourceReservation &reservation, GameplayContext) noexcept override
    {
        struct Payload
        {
            std::int64_t amount;
        };
        auto payload = reservation.provider_token.AsTrivial<Payload>(TypeId::FromString("p"));
        if (payload)
            balance += payload->amount;
    }

    [[nodiscard]] foundation::Result<void> ReconcileReservation(const AbilityResourceReservation &reservation,
                                                                GameplayObjectRef,
                                                                GameplayContext) override
    {
        if (!reservation.id.IsValid())
            return foundation::Result<void>::Failure(foundation::Error::Create("reconcile", "invalid reservation"));
        ++reconcile_calls;
        if (fail_reconcile_call != 0 && reconcile_calls == fail_reconcile_call)
            return foundation::Result<void>::Failure(foundation::Error::Create("reconcile", "forced reconcile failure"));
        ++reconciled;
        return foundation::Result<void>::Success();
    }
};


class NoAllocResource final : public IAbilityResourceProvider
{
  public:
    std::int64_t balance = 100;
    std::uint64_t reserve_calls = 0;
    std::uint64_t release_calls = 0;
    std::uint64_t commit_calls = 0;

    [[nodiscard]] bool CanAfford(GameplayObjectRef, AbilityResourceTypeId, std::int64_t amount) const override
    {
        return balance >= amount;
    }

    [[nodiscard]] foundation::Result<AbilityResourceReservation> Reserve(GameplayObjectRef, AbilityResourceTypeId type,
                                                                         std::int64_t amount, GameplayContext) override
    {
        if (balance < amount)
            return foundation::Result<AbilityResourceReservation>::Failure(
                foundation::Error::Create("no", "insufficient resource"));
        balance -= amount;
        ++reserve_calls;
        return foundation::Result<AbilityResourceReservation>::Success(
            {AbilityReservationId{GameplayObjectId::FromRaw(0xA11, reserve_calls)}, type, {}});
    }

    void Commit(const AbilityResourceReservation &, GameplayContext) noexcept override { ++commit_calls; }

    void Release(const AbilityResourceReservation &, GameplayContext) noexcept override
    {
        balance += 10;
        ++release_calls;
    }
};

class Materialization final : public IAbilityMaterializationProvider
{
  public:
    bool materialized = true;
    [[nodiscard]] bool IsMaterialized(GameplayObjectRef) const noexcept override { return materialized; }
};
} // namespace

int main()
{
    GameplayObjectRef owner{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("mage")};
    GameplayObjectRef target{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("enemy")};

    AbilityService s;
    Resource res;
    s.SetResourceProvider(&res);

    AbilityDefinition d;
    d.canonical_name = "game.fireball";
    d.targeting = AbilityTargetPolicy::SingleTarget;
    d.timing.kind = AbilityTimingKind::CastTime;
    d.timing.cast_duration = {5};
    d.cooldown.group = CooldownGroupId::FromString("game.gcd");
    d.cooldown.duration = {10};
    AbilityCostDefinition c;
    c.resource = AbilityResourceTypeId::FromString("game.mana");
    c.amount_micro = 20;
    c.policy = AbilityCostPolicy::ReserveThenCommit;
    d.costs.push_back(c);
    d.outputs.push_back({ActionTypeId::FromString("game.damage"), 1'000'000, {}});
    auto did = s.RegisterDefinition(d);
    if (!did)
        return 1;
    s.Freeze();

    auto iid = s.Grant(owner, did.Value());
    if (!iid)
        return 2;
    AbilityTargetSet targets;
    targets.primary = target;
    auto exec = s.BeginActivation({iid.Value(), targets, {0}, {}});
    if (!exec || res.balance != 80)
        return 3;
    if (s.CompleteExecution(exec.Value(), {4}))
        return 4;
    auto out = s.CompleteExecution(exec.Value(), {5});
    if (!out || out.Value().size() != 1 || res.balance != 80 || s.FindExecution(exec.Value()) != nullptr)
        return 5;
    if (s.CanActivate(iid.Value(), targets, {6}).availability != AbilityAvailability::Unavailable)
        return 6;
    if (s.CanActivate(iid.Value(), targets, {15}).availability != AbilityAvailability::Available)
        return 7;
    s.SweepCooldowns({15});
    if (s.GetDiagnostics().cooldowns != 0)
        return 8;

    auto exec2 = s.BeginActivation({iid.Value(), targets, {15}, {}});
    if (!exec2 || res.balance != 60)
        return 9;
    const auto interrupt_reason = TypeId::FromString("test.interrupt");
    if (!s.Interrupt(exec2.Value(), interrupt_reason, {16}) || res.balance != 80 ||
        s.FindExecution(exec2.Value()) != nullptr)
        return 10;
    const auto changes = s.ReadChangesSince(ChangeCursor{}).changes;
    bool saw_reason = false;
    for (const auto &change : changes)
        if (change.kind == AbilityChangeKind::Interrupted && change.execution == exec2.Value() &&
            change.reason == interrupt_reason)
            saw_reason = true;
    if (!saw_reason)
        return 11;

    if (!s.SetAbilityEnabled(iid.Value(), false) ||
        s.CanActivate(iid.Value(), targets, {20}).availability != AbilityAvailability::Unavailable ||
        !s.SetAbilityEnabled(iid.Value(), true))
        return 12;

    AbilityService tx;
    Resource tx_res;
    tx.SetResourceProvider(&tx_res);
    AbilityDefinition tx_def;
    tx_def.canonical_name = "game.combo";
    tx_def.timing.kind = AbilityTimingKind::Instant;
    AbilityCostDefinition a{AbilityResourceTypeId::FromString("game.mana"), 10, AbilityCostPolicy::PayOnStart};
    AbilityCostDefinition b{AbilityResourceTypeId::FromString("game.stamina"), 20, AbilityCostPolicy::PayOnStart};
    AbilityCostDefinition fail{AbilityResourceTypeId::FromString("game.charge"), 1, AbilityCostPolicy::PayOnStart};
    tx_res.fail_reserve = fail.resource;
    tx_def.costs = {a, b, fail};
    tx_def.outputs.push_back({ActionTypeId::FromString("game.combo.out"), 1, {}});
    auto tx_did = tx.RegisterDefinition(tx_def);
    if (!tx_did)
        return 13;
    tx.Freeze();
    auto tx_iid = tx.Grant(owner, tx_did.Value());
    if (!tx_iid)
        return 14;
    auto failed = tx.BeginActivation({tx_iid.Value(), {}, {20}, {}});
    if (failed || tx_res.balance != 100)
        return 15;

    AbilityService staged;
    Resource staged_res;
    staged_res.balance = 0;
    staged.SetResourceProvider(&staged_res);
    AbilityDefinition staged_def;
    staged_def.canonical_name = "game.pay_later";
    staged_def.timing.kind = AbilityTimingKind::Instant;
    staged_def.costs.push_back(
        {AbilityResourceTypeId::FromString("game.future"), 10, AbilityCostPolicy::PayOnExecute});
    auto staged_did = staged.RegisterDefinition(staged_def);
    if (!staged_did)
        return 16;
    staged.Freeze();
    auto staged_iid = staged.Grant(owner, staged_did.Value());
    if (!staged_iid || staged.CanActivate(staged_iid.Value(), {}, {0}).availability != AbilityAvailability::Available)
        return 17;
    auto staged_exec = staged.BeginActivation({staged_iid.Value(), {}, {0}, {}});
    if (!staged_exec)
        return 18;
    if (staged.CompleteExecution(staged_exec.Value(), {0}) || staged.FindExecution(staged_exec.Value()) != nullptr)
        return 19;

    AbilityService continuity;
    Materialization materialization;
    continuity.SetMaterializationProvider(&materialization);
    AbilityDefinition continuity_def;
    continuity_def.canonical_name = "game.continuity";
    continuity_def.targeting = AbilityTargetPolicy::SingleTarget;
    continuity_def.timing.kind = AbilityTimingKind::CastTime;
    continuity_def.timing.cast_duration = {2};
    continuity_def.requires_materialized_owner = true;
    continuity_def.requires_materialized_target = true;
    continuity_def.continuity = AbilityContinuityPolicy::UntilExecute;
    auto continuity_did = continuity.RegisterDefinition(continuity_def);
    if (!continuity_did)
        return 20;
    continuity.Freeze();
    auto continuity_iid = continuity.Grant(owner, continuity_did.Value());
    if (!continuity_iid)
        return 21;
    auto continuity_exec = continuity.BeginActivation({continuity_iid.Value(), targets, {0}, {}});
    if (!continuity_exec)
        return 22;
    materialization.materialized = false;
    if (continuity.CompleteExecution(continuity_exec.Value(), {2}) ||
        continuity.FindExecution(continuity_exec.Value()) != nullptr)
        return 23;

    AbilityService original;
    Resource original_res;
    original.SetResourceProvider(&original_res);
    AbilityDefinition persisted_def = d;
    persisted_def.canonical_name = "game.persisted";
    persisted_def.id = {};
    auto persisted_did = original.RegisterDefinition(persisted_def);
    if (!persisted_did)
        return 24;
    original.Freeze();
    auto persisted_iid = original.Grant(owner, persisted_did.Value());
    if (!persisted_iid)
        return 25;
    auto persisted_exec = original.BeginActivation({persisted_iid.Value(), targets, {0}, {}});
    if (!persisted_exec)
        return 26;
    auto snapshot = original.CaptureSnapshot();

    AbilityService restored;
    Resource restored_res;
    restored.SetResourceProvider(&restored_res);
    AbilityDefinition restored_def = persisted_def;
    restored_def.id = {};
    auto restored_did = restored.RegisterDefinition(restored_def);
    if (!restored_did)
        return 27;
    restored.Freeze();
    if (!restored.RestoreSnapshot(snapshot) || !restored.NeedsResourceReconciliation())
        return 28;
    if (restored.CompleteExecution(persisted_exec.Value(), {5}))
        return 29;
    if (!restored.ReconcileRestoredReservations() || restored.NeedsResourceReconciliation() || restored_res.reconciled != 1)
        return 30;
    if (!restored.CompleteExecution(persisted_exec.Value(), {5}) || restored.FindExecution(persisted_exec.Value()) != nullptr)
        return 31;

    auto corrupt = original.CaptureSnapshot();
    corrupt.instance_ids.scope ^= 1u;
    AbilityService guarded;
    Resource guarded_res;
    guarded.SetResourceProvider(&guarded_res);
    AbilityDefinition guarded_def = persisted_def;
    guarded_def.id = {};
    if (!guarded.RegisterDefinition(guarded_def))
        return 32;
    guarded.Freeze();
    if (guarded.RestoreSnapshot(corrupt))
        return 33;

    // ABL-08/09: partial reservation reconciliation is resumable and does not replay completed entries.
    AbilityService reconcile_source;
    Resource reconcile_source_res;
    reconcile_source.SetResourceProvider(&reconcile_source_res);
    AbilityDefinition reconcile_def;
    reconcile_def.canonical_name = "game.reconcile_two";
    reconcile_def.timing.kind = AbilityTimingKind::CastTime;
    reconcile_def.timing.cast_duration = {1};
    reconcile_def.costs.push_back({AbilityResourceTypeId::FromString("game.r1"), 10, AbilityCostPolicy::ReserveThenCommit});
    reconcile_def.costs.push_back({AbilityResourceTypeId::FromString("game.r2"), 10, AbilityCostPolicy::ReserveThenCommit});
    auto reconcile_did = reconcile_source.RegisterDefinition(reconcile_def);
    if (!reconcile_did)
        return 34;
    reconcile_source.Freeze();
    auto reconcile_iid = reconcile_source.Grant(owner, reconcile_did.Value());
    if (!reconcile_iid)
        return 35;
    auto reconcile_exec = reconcile_source.BeginActivation({reconcile_iid.Value(), {}, {0}, {}});
    if (!reconcile_exec)
        return 36;
    const auto reconcile_snapshot = reconcile_source.CaptureSnapshot();

    AbilityService reconcile_restored;
    Resource reconcile_res;
    reconcile_res.fail_reconcile_call = 2;
    reconcile_restored.SetResourceProvider(&reconcile_res);
    AbilityDefinition reconcile_restore_def = reconcile_def;
    reconcile_restore_def.id = {};
    if (!reconcile_restored.RegisterDefinition(reconcile_restore_def))
        return 37;
    reconcile_restored.Freeze();
    if (!reconcile_restored.RestoreSnapshot(reconcile_snapshot))
        return 38;
    if (reconcile_restored.ReconcileRestoredReservations() || reconcile_res.reconcile_calls != 2 ||
        reconcile_res.reconciled != 1)
        return 39;
    reconcile_res.fail_reconcile_call = 0;
    if (!reconcile_restored.ReconcileRestoredReservations() || reconcile_res.reconcile_calls != 3 ||
        reconcile_res.reconciled != 2 || reconcile_restored.NeedsResourceReconciliation())
        return 40;

    // ABL-07/09: exhausted instance revision rejects mutation without changing the instance.
    auto revision_snapshot = reconcile_source.CaptureSnapshot();
    if (revision_snapshot.instances.empty())
        return 41;
    revision_snapshot.instances.front().revision.value = std::numeric_limits<std::uint64_t>::max();
    AbilityService revision_guard;
    AbilityDefinition revision_def = reconcile_def;
    revision_def.id = {};
    if (!revision_guard.RegisterDefinition(revision_def))
        return 42;
    revision_guard.Freeze();
    if (!revision_guard.RestoreSnapshot(revision_snapshot))
        return 43;
    const auto revision_instance = revision_snapshot.instances.front().id;
    const auto before_revision_change = revision_guard.FindInstance(revision_instance)->revision;
    if (revision_guard.SetAbilityEnabled(revision_instance, false) ||
        !revision_guard.FindInstance(revision_instance)->enabled ||
        revision_guard.FindInstance(revision_instance)->revision != before_revision_change)
        return 44;

    // ABL-05/09: failed schedule rebind preserves the old binding and execution revision.
    AbilityService bind_service;
    NoAllocResource bind_res;
    bind_service.SetResourceProvider(&bind_res);
    AbilityDefinition bind_def;
    bind_def.canonical_name = "game.bind";
    bind_def.timing.kind = AbilityTimingKind::CastTime;
    bind_def.timing.cast_duration = {5};
    auto bind_did = bind_service.RegisterDefinition(bind_def);
    if (!bind_did)
        return 45;
    bind_service.Freeze();
    auto bind_iid = bind_service.Grant(owner, bind_did.Value());
    if (!bind_iid)
        return 46;
    auto bind_exec = bind_service.BeginActivation({bind_iid.Value(), {}, {0}, {}});
    if (!bind_exec)
        return 47;
    const auto old_schedule = ScheduleId::FromString("test.old_schedule");
    const auto new_schedule = ScheduleId::FromString("test.new_schedule");
    if (!bind_service.BindSchedule(bind_exec.Value(), old_schedule))
        return 48;
    const auto before_bind = bind_service.CaptureSnapshot();
    {
        test_seam::FailNext(test_seam::FaultPoint::BindScheduleBeforePublish);
        auto rebound = bind_service.BindSchedule(bind_exec.Value(), new_schedule);
        if (rebound)
            return 49;
    }
    const auto after_bind = bind_service.CaptureSnapshot();
    if (!SameSnapshot(before_bind, after_bind))
        return 50;

    // ABL-01/09 + G4-INFRA-001: module-local fault after provider reservation is failure-atomic.
    AbilityService atomic_service;
    NoAllocResource atomic_res;
    atomic_service.SetResourceProvider(&atomic_res);
    AbilityDefinition atomic_def;
    atomic_def.canonical_name = "game.atomic_activation";
    atomic_def.timing.kind = AbilityTimingKind::CastTime;
    atomic_def.timing.cast_duration = {1};
    atomic_def.costs.push_back({AbilityResourceTypeId::FromString("game.atomic"), 10,
                                AbilityCostPolicy::ReserveThenCommit});
    auto atomic_did = atomic_service.RegisterDefinition(atomic_def);
    if (!atomic_did)
        return 51;
    atomic_service.Freeze();
    auto atomic_iid = atomic_service.Grant(owner, atomic_did.Value());
    if (!atomic_iid)
        return 52;
    const auto before = atomic_service.CaptureSnapshot();
    test_seam::FailNext(test_seam::FaultPoint::BeginActivationBeforePublish);
    auto attempted = atomic_service.BeginActivation({atomic_iid.Value(), {}, {0}, {}});
    if (attempted || atomic_res.reserve_calls == 0)
        return 53;
    const auto after = atomic_service.CaptureSnapshot();
    if (atomic_res.balance != 100 || !SameSnapshot(before, after))
        return 54;
    if (!atomic_service.BeginActivation({atomic_iid.Value(), {}, {0}, {}}))
        return 60;

    // ABL-02/09: output allocation failure cannot commit held resources or consume the execution.
    AbilityService output_service;
    NoAllocResource output_res;
    output_service.SetResourceProvider(&output_res);
    AbilityDefinition output_def;
    output_def.canonical_name = "game.output_atomic";
    output_def.timing.kind = AbilityTimingKind::CastTime;
    output_def.timing.cast_duration = {1};
    output_def.costs.push_back({AbilityResourceTypeId::FromString("game.output_resource"), 10,
                                AbilityCostPolicy::ReserveThenCommit});
    output_def.outputs.push_back({ActionTypeId::FromString("game.output"), 1, {}});
    auto output_did = output_service.RegisterDefinition(output_def);
    if (!output_did)
        return 55;
    output_service.Freeze();
    auto output_iid = output_service.Grant(owner, output_did.Value());
    auto output_exec = output_iid ? output_service.BeginActivation({output_iid.Value(), {}, {0}, {}})
                                  : foundation::Result<AbilityExecutionId>::Failure(
                                        foundation::Error::Create("test", "grant failed"));
    if (!output_exec || output_res.balance != 90)
        return 56;
    const auto before_output_fault = output_service.CaptureSnapshot();
    {
        test_seam::FailNext(test_seam::FaultPoint::CompleteExecutionBeforeOutputs);
        auto failed_output = output_service.CompleteExecution(output_exec.Value(), {1});
        if (failed_output)
            return 57;
    }
    const auto after_output_fault = output_service.CaptureSnapshot();
    if (output_res.commit_calls != 0 || output_res.balance != 90 || !output_service.FindExecution(output_exec.Value()) ||
        !SameSnapshot(before_output_fault, after_output_fault))
        return 58;
    if (!output_service.CompleteExecution(output_exec.Value(), {1}) || output_res.commit_calls != 1)
        return 59;

    // Restore staging failures leave the full live snapshot unchanged; each fault starts from a fresh fixture.
    const auto restore_seed = output_service.CaptureSnapshot();
    for (const auto point : {test_seam::FaultPoint::RestoreCandidateBuild, test_seam::FaultPoint::RestoreBeforeCommit})
    {
        auto restore_service = output_service;
        const auto restore_before = restore_service.CaptureSnapshot();
        auto restore_target = restore_seed;
        test_seam::FailNext(point);
        if (restore_service.RestoreSnapshot(std::move(restore_target)))
            return 61;
        const auto restore_after = restore_service.CaptureSnapshot();
        if (!SameSnapshot(restore_before, restore_after))
            return 62;
    }

    return 0;
}
