#include "Epidemic/GameFramework/NeedsLife/needs_life.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::needs_life;

namespace
{
void Check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char *domain, const char *id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}
} // namespace

int main()
{
    NeedsLifeService service;

    NeedDecayRule fast;
    fast.id = NeedDecayRuleId::FromString("need.decay.fast");
    fast.rate_multiplier_micro = 2'000'000;
    Check(static_cast<bool>(service.RegisterDecayRule(fast)), "register decay rule");

    LifeSimulationProfile coarse;
    coarse.id = LifeSimulationProfileId::FromString("life.sim.coarse");
    coarse.abstract_rate_multiplier_micro = 500'000;
    coarse.materialized_rate_multiplier_micro = 2'000'000;
    Check(static_cast<bool>(service.RegisterSimulationProfile(coarse)), "register simulation profile");

    NeedDefinition hunger;
    hunger.id = NeedTypeId::FromString("need.hunger");
    hunger.default_value = 100;
    hunger.min_value = 0;
    hunger.max_value = 100;
    hunger.decay_per_tick = 1;
    hunger.decay_rule = fast.id;
    hunger.satisfied_threshold_micro = 800'000;
    hunger.low_threshold_micro = 600'000;
    hunger.medium_threshold_micro = 400'000;
    hunger.high_threshold_micro = 200'000;
    Check(static_cast<bool>(service.RegisterNeedDefinition(hunger)), "register hunger");
    Check(static_cast<bool>(service.FreezeDefinitions()), "freeze definitions");
    Check(!service.RegisterNeedDefinition(hunger), "definitions immutable after freeze");

    const auto npc = Ref("population", "npc.worker");
    NeedProfile profile;
    profile.subject = npc;
    profile.active_needs.push_back(hunger.id);
    profile.simulation_profile = coarse.id;
    auto profile_id = service.CreateNeedProfile(profile, {.time = GameplayTimePoint{0}});
    Check(static_cast<bool>(profile_id), "create profile");
    Check(!service.CreateNeedProfile(profile), "only one active profile per subject");

    // Base read-side evaluation uses the need's decay rule. 2x decay for 10 ticks -> 80.
    auto evaluated = service.EvaluateNeed(npc, hunger.id, GameplayTimePoint{10});
    Check(evaluated.value == 80, "decay rule applied to lazy evaluation");
    Check(evaluated.threshold == NeedThreshold::Satisfied, "data-driven threshold applied");

    // Abstract simulation applies the profile's 0.5 multiplier, cancelling the rule's 2x multiplier.
    LifeSimulationRequest first_interval;
    first_interval.subject = npc;
    first_interval.from = GameplayTimePoint{0};
    first_interval.to = GameplayTimePoint{10};
    first_interval.context.time = GameplayTimePoint{10};
    first_interval.materialized = false;
    Check(static_cast<bool>(service.SimulateLifeInterval(first_interval)), "simulate first interval");
    Check(service.GetCommittedNeedState(npc, hunger.id)->value == 90, "simulation profile applied");

    // Retry/overlap is rejected by the authoritative profile cursor.
    Check(!service.SimulateLifeInterval(first_interval), "duplicate interval rejected");
    auto overlap = first_interval;
    overlap.from = GameplayTimePoint{5};
    overlap.to = GameplayTimePoint{15};
    Check(!service.SimulateLifeInterval(overlap), "overlapping interval rejected");

    auto second_interval = first_interval;
    second_interval.from = GameplayTimePoint{10};
    second_interval.to = GameplayTimePoint{20};
    second_interval.context.time = GameplayTimePoint{20};
    second_interval.materialized = true;
    Check(static_cast<bool>(service.SimulateLifeInterval(second_interval)), "materialized interval");
    Check(service.GetCommittedNeedState(npc, hunger.id)->value == 50, "materialized fidelity multiplier applied");

    SatisfyNeedRequest satisfy;
    satisfy.subject = npc;
    satisfy.need = hunger.id;
    satisfy.amount = -1;
    satisfy.context.time = GameplayTimePoint{20};
    Check(!service.SatisfyNeed(satisfy), "negative satisfy rejected");
    satisfy.amount = 40;
    Check(static_cast<bool>(service.SatisfyNeed(satisfy)), "satisfy need");
    Check(service.GetCommittedNeedState(npc, hunger.id)->value == 90, "satisfy increases value");

    AddNeedPressureRequest pressure_delta;
    pressure_delta.subject = npc;
    pressure_delta.need = hunger.id;
    pressure_delta.amount = 30;
    pressure_delta.context.time = GameplayTimePoint{20};
    Check(static_cast<bool>(service.AddNeedPressure(pressure_delta)), "add need pressure");
    Check(service.GetCommittedNeedState(npc, hunger.id)->value == 60, "pressure decreases need value");

    LifePressure pressure;
    pressure.subject = npc;
    pressure.type = TypeId::FromString("pressure.safety");
    pressure.source = TypeId::FromString("source.duty");
    pressure.urgency = 500;
    pressure.expires_at = GameplayTimePoint{30};
    auto pressure_id = service.CreateLifePressure(pressure, {.time = GameplayTimePoint{20}});
    Check(static_cast<bool>(pressure_id), "create pressure");
    Check(service.FindLifePressures(npc).size() == 1, "find pressure");
    auto expired = service.SweepExpiredPressures(GameplayTimePoint{30}, {.time = GameplayTimePoint{30}});
    Check(static_cast<bool>(expired) && expired.Value() == 1, "expire pressure");
    Check(service.FindLifePressures(npc).empty(), "expired pressure not active");
    Check(service.PruneTerminalPressures(npc) == 1, "terminal pressure cleanup");

    LifeRoutine routine;
    routine.subject = npc;
    RoutineEntry entry;
    entry.start = GameplayTimePoint{800};
    entry.duration = GameplayDuration{400};
    entry.routine_type = TypeId::FromString("routine.work");
    routine.entries.push_back(entry);
    auto routine_id = service.SetRoutine(routine);
    Check(static_cast<bool>(routine_id), "set routine");
    routine.entries[0].start = GameplayTimePoint{900};
    auto updated_routine = service.SetRoutine(routine);
    Check(static_cast<bool>(updated_routine) && updated_routine.Value() == routine_id.Value(), "routine upsert by subject");
    auto occurrences = service.FindRoutineEntriesInInterval(npc, GameplayTimePoint{850}, GameplayTimePoint{950});
    Check(occurrences.size() == 1 && occurrences.front().routine == routine_id.Value() &&
              occurrences.front().entry_index == 0,
          "stable routine occurrence");

    // MaterializedOnly has explicit behavior instead of being a decorative enum.
    const auto npc2 = Ref("population", "npc.materialized");
    NeedProfile materialized_profile;
    materialized_profile.subject = npc2;
    materialized_profile.active_needs.push_back(hunger.id);
    materialized_profile.materialization_policy = NeedMaterializationPolicy::MaterializedOnly;
    Check(static_cast<bool>(service.CreateNeedProfile(materialized_profile, {.time = GameplayTimePoint{0}})),
          "create materialized profile");
    LifeSimulationRequest materialized_request;
    materialized_request.subject = npc2;
    materialized_request.from = GameplayTimePoint{0};
    materialized_request.to = GameplayTimePoint{1};
    Check(!service.SimulateLifeInterval(materialized_request), "materialized-only rejects abstract simulation");
    materialized_request.materialized = true;
    Check(static_cast<bool>(service.SimulateLifeInterval(materialized_request)), "materialized-only accepts materialized");

    // Journal is bounded and reports a gap instead of pretending retained changes are complete.
    service.SetChangeJournalCapacity(2);
    SatisfyNeedRequest small = satisfy;
    small.amount = 1;
    small.context.time = GameplayTimePoint{21};
    Check(static_cast<bool>(service.SatisfyNeed(small)), "journal change 1");
    small.context.time = GameplayTimePoint{22};
    Check(static_cast<bool>(service.SatisfyNeed(small)), "journal change 2");
    small.context.time = GameplayTimePoint{23};
    Check(static_cast<bool>(service.SatisfyNeed(small)), "journal change 3");
    Check(service.ReadChangesSince(ChangeCursor{}).snapshot_required, "journal gap requires snapshot");
    Check(service.ReadChangesSince(service.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required,
          "future needs cursor is incompatible");
    const auto pre_restore_cursor = service.ReadChangesSince(ChangeCursor{}).latest_cursor;

    const auto snapshot = service.CaptureSnapshot();
    NeedsLifeService restored;
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)), "restore valid snapshot");
    Check(restored.GetNeedProfile(npc) != nullptr, "restore profile index");
    Check(restored.GetCommittedNeedState(npc, hunger.id) != nullptr, "restore need state");
    Check(restored.ReadChangesSince(pre_restore_cursor).snapshot_required,
          "pre-restore needs cursor requires snapshot in new epoch");
    Check(restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required,
          "future needs cursor remains incompatible after restore");
    small.context.time = GameplayTimePoint{24};
    Check(static_cast<bool>(restored.SatisfyNeed(small)), "post-restore needs change");
    Check(restored.ReadChangesSince(pre_restore_cursor).snapshot_required,
          "old needs cursor remains incompatible after post-restore change");
    const auto needs_epoch = restored.ReadChangesSince(ChangeCursor{});
    Check(!needs_epoch.snapshot_required && !needs_epoch.changes.empty(), "new needs epoch readable from zero");
    const auto needs_current = restored.ReadChangesSince(needs_epoch.latest_cursor);
    Check(!needs_current.snapshot_required && needs_current.changes.empty(), "exact needs cursor is current");

    // Restore is transactional. A broken snapshot must not erase the current service.
    const auto before_revision = restored.CurrentRevision();
    auto broken = snapshot;
    auto duplicate = broken.profiles.front();
    duplicate.id = NeedProfileId::FromRaw(0x2900, 9999);
    broken.profiles.push_back(duplicate);
    Check(!restored.RestoreSnapshot(broken), "duplicate subject profile rejected on restore");
    Check(restored.CurrentRevision() == before_revision && restored.GetNeedProfile(npc) != nullptr,
          "failed restore leaves service unchanged");

    broken = snapshot;
    broken.profile_ids.scope = 0xDEAD;
    Check(!restored.RestoreSnapshot(broken), "wrong generator scope rejected");
    Check(restored.CurrentRevision() == before_revision, "generator failure leaves service unchanged");

    // A requested ID in the service's own scope advances the generator and cannot collide later.
    NeedsLifeService requested_id_service;
    Check(static_cast<bool>(requested_id_service.RegisterDecayRule(fast)), "register requested-id decay");
    Check(static_cast<bool>(requested_id_service.RegisterNeedDefinition(hunger)), "register requested-id hunger");
    NeedProfile requested;
    requested.id = NeedProfileId::FromRaw(0x2900, 100);
    requested.subject = Ref("population", "requested");
    requested.active_needs.push_back(hunger.id);
    Check(static_cast<bool>(requested_id_service.CreateNeedProfile(requested)), "requested own-scope profile id");
    NeedProfile generated;
    generated.subject = Ref("population", "generated");
    generated.active_needs.push_back(hunger.id);
    auto generated_id = requested_id_service.CreateNeedProfile(generated);
    Check(static_cast<bool>(generated_id) && generated_id.Value().value.Low() > 100, "generator advanced past requested id");


    // Forged profile policy must not consume an id or publish a revision.
    const auto needs_invalid_before = requested_id_service.CaptureSnapshot();
    NeedProfile invalid_policy_profile;
    invalid_policy_profile.subject = Ref("population", "invalid-policy");
    invalid_policy_profile.active_needs.push_back(hunger.id);
    invalid_policy_profile.materialization_policy = static_cast<NeedMaterializationPolicy>(999);
    Check(!requested_id_service.CreateNeedProfile(invalid_policy_profile), "invalid materialization policy rejected");
    const auto needs_invalid_after = requested_id_service.CaptureSnapshot();
    Check(needs_invalid_after.revision == needs_invalid_before.revision &&
              needs_invalid_after.profile_ids.next == needs_invalid_before.profile_ids.next &&
              needs_invalid_after.profiles.size() == needs_invalid_before.profiles.size(),
          "invalid profile leaves id/revision/state unchanged");

    // Exhausted revision rejects a mutation without changing the committed need value.
    auto needs_revision_exhausted_snapshot = snapshot;
    needs_revision_exhausted_snapshot.revision.value = std::numeric_limits<std::uint64_t>::max();
    NeedsLifeService needs_revision_exhausted;
    Check(static_cast<bool>(needs_revision_exhausted.RestoreSnapshot(needs_revision_exhausted_snapshot)),
          "restore max needs revision");
    const auto *need_before_exhaustion = needs_revision_exhausted.GetCommittedNeedState(npc, hunger.id);
    Check(need_before_exhaustion != nullptr, "need exists before revision exhaustion test");
    const auto exhausted_value_before = need_before_exhaustion->value;
    SatisfyNeedRequest exhausted_satisfy = satisfy;
    exhausted_satisfy.amount = 1;
    exhausted_satisfy.context.time = GameplayTimePoint{30};
    Check(!needs_revision_exhausted.SatisfyNeed(exhausted_satisfy), "needs revision exhaustion rejected");
    const auto *need_after_exhaustion = needs_revision_exhausted.GetCommittedNeedState(npc, hunger.id);
    Check(need_after_exhaustion != nullptr && need_after_exhaustion->value == exhausted_value_before &&
              needs_revision_exhausted.CurrentRevision().value == std::numeric_limits<std::uint64_t>::max(),
          "needs revision exhaustion leaves state unchanged");

    return 0;
}
