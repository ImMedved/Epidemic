#include "../../GameplayWorldStateOwners/Progression/src/progression_test_seam.h"
#include "Epidemic/GameFramework/Progression/progression.h"
#include <algorithm>
#include <limits>
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::progression;
namespace
{
template <class T, class Pred>
bool SameVector(const std::vector<T>& a, const std::vector<T>& b, Pred pred)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), pred);
}

bool SameTrack(const ProgressionTrackState& a, const ProgressionTrackState& b)
{
    return a.id == b.id && a.progress_micro == b.progress_micro && a.rank == b.rank && a.revision == b.revision;
}

bool SameModifier(const ProgressionModifier& a, const ProgressionModifier& b)
{
    return a.id == b.id && a.target == b.target && a.operation == b.operation && a.value_micro == b.value_micro &&
           a.priority == b.priority && a.modifier_type == b.modifier_type && a.source == b.source &&
           a.persistent == b.persistent;
}

bool SameProfile(const ProgressionProfileSnapshot& a, const ProgressionProfileSnapshot& b)
{
    return a.subject == b.subject && a.base_attributes == b.base_attributes &&
           SameVector(a.tracks, b.tracks, SameTrack) && SameVector(a.modifiers, b.modifiers, SameModifier) &&
           a.perks == b.perks && a.unlocks == b.unlocks && a.achieved_milestones == b.achieved_milestones &&
           a.revision == b.revision;
}

bool SameChange(const ProgressionChange& a, const ProgressionChange& b)
{
    return a.sequence == b.sequence && a.kind == b.kind && a.subject == b.subject && a.attribute == b.attribute &&
           a.track == b.track && a.perk == b.perk && a.modifier == b.modifier && a.revision == b.revision &&
           a.context == b.context && a.milestone == b.milestone && a.unlock_type == b.unlock_type &&
           a.unlock_value == b.unlock_value;
}

bool SameSnapshot(const ProgressionSnapshot& a, const ProgressionSnapshot& b)
{
    return SameVector(a.profiles, b.profiles, SameProfile) &&
           a.modifier_ids.scope == b.modifier_ids.scope && a.modifier_ids.next == b.modifier_ids.next &&
           a.grant_reservation_ids.scope == b.grant_reservation_ids.scope &&
           a.grant_reservation_ids.next == b.grant_reservation_ids.next && a.revision == b.revision &&
           SameVector(a.journal, b.journal, SameChange) && a.next_change_sequence == b.next_change_sequence &&
           a.change_epoch == b.change_epoch;
}
}
int main()
{
    ProgressionService s;
    AttributeDefinition strength;
    strength.canonical_name = "game.strength";
    strength.default_micro = 1'000'000;
    strength.min_micro = 0;
    strength.max_micro = 100'000'000;
    auto sid = s.RegisterAttribute(strength);
    if (!sid)
        return 1;
    AttributeDefinition power;
    power.canonical_name = "game.power";
    power.min_micro = 0;
    power.max_micro = 500'000'000;
    power.derived_terms.push_back({sid.Value(), 2'000'000});
    auto pid = s.RegisterAttribute(power);
    if (!pid)
        return 2;
    ProgressionTrackDefinition level;
    level.canonical_name = "game.level";
    level.rank_thresholds_micro = {100, 300};
    auto tid = s.RegisterTrack(level);
    if (!tid)
        return 3;
    PerkDefinition perk;
    perk.canonical_name = "game.perk.a";
    auto perkid = s.RegisterPerk(perk);
    if (!perkid || !s.Freeze())
        return 4;
    GameplayObjectRef actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor")};
    if (!s.EnsureProfile(actor) || !s.SetBaseAttribute(actor, sid.Value(), 10'000'000))
        return 5;
    auto value = s.GetAttribute(actor, pid.Value());
    if (!value || value.Value() != 20'000'000)
        return 6;
    ProgressionModifier m;
    m.target = pid.Value();
    m.operation = ModifierOperation::FinalAdd;
    m.value_micro = 5'000'000;
    m.modifier_type = TypeId::FromString("test.mod");
    auto mid = s.AddModifier(actor, m);
    if (!mid)
        return 7;
    value = s.GetAttribute(actor, pid.Value());
    if (!value || value.Value() != 25'000'000)
        return 8;
    auto track = s.GrantProgress(actor, tid.Value(), 150);
    if (!track || track.Value().rank != 1)
        return 9;
    if (!s.GrantPerk(actor, perkid.Value()) || !s.HasPerk(actor, perkid.Value()))
        return 10;
    auto snap = s.CaptureSnapshot();
    ProgressionService restored;
    auto rs = restored.RegisterAttribute(strength);
    auto rp = restored.RegisterAttribute(power);
    auto rt = restored.RegisterTrack(level);
    auto rk = restored.RegisterPerk(perk);
    if (!rs || !rp || !rt || !rk || !restored.Freeze() || !restored.RestoreSnapshot(std::move(snap)))
        return 11;
    auto rv = restored.GetAttribute(actor, pid.Value());
    if (!rv || rv.Value() != 25'000'000)
        return 12;

    // C04: reservations are invisible until commit and serialize conflicting progression mutations.
    auto reserved = s.ReserveProgressGrant(actor, tid.Value(), 50);
    if (!reserved)
        return 13;
    auto during_reserve = s.GetTrack(actor, tid.Value());
    if (!during_reserve || during_reserve.Value().progress_micro != 150)
        return 14;
    if (s.ReserveProgressGrant(actor, tid.Value(), 1))
        return 15;
    if (s.GrantProgress(actor, tid.Value(), 1))
        return 16;
    const auto pending_snapshot = s.CaptureSnapshot();
    const auto pending_profile = std::find_if(pending_snapshot.profiles.begin(), pending_snapshot.profiles.end(),
                                              [&](const auto& profile) { return profile.subject == actor; });
    if (pending_profile == pending_snapshot.profiles.end())
        return 17;
    const auto pending_track = std::find_if(pending_profile->tracks.begin(), pending_profile->tracks.end(),
                                            [&](const auto& state) { return state.id == tid.Value(); });
    if (pending_track == pending_profile->tracks.end() || pending_track->progress_micro != 150)
        return 18;
    s.ReleaseProgressGrant(reserved.Value());
    auto after_release = s.GetTrack(actor, tid.Value());
    if (!after_release || after_release.Value().progress_micro != 150)
        return 19;

    reserved = s.ReserveProgressGrant(actor, tid.Value(), 50);
    if (!reserved)
        return 20;
    s.CommitProgressGrant(reserved.Value());
    s.CommitProgressGrant(reserved.Value());
    auto after_commit = s.GetTrack(actor, tid.Value());
    if (!after_commit || after_commit.Value().progress_micro != 200)
        return 21;

    // H32: profile cleanup is blocked while a reservation is active and succeeds after release.
    GameplayObjectRef transient{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("transient")};
    if (!s.EnsureProfile(transient))
        return 22;
    auto transient_reservation = s.ReserveProgressGrant(transient, tid.Value(), 10);
    if (!transient_reservation || s.RemoveProfile(transient))
        return 23;
    s.ReleaseProgressGrant(transient_reservation.Value());
    if (!s.RemoveProfile(transient) || s.HasProfile(transient))
        return 24;

    // M35: modifier IDs are service-wide and caller-supplied IDs advance the own-scope generator.
    GameplayObjectRef actor_b{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor-b")};
    if (!s.EnsureProfile(actor_b))
        return 25;
    const auto modifier_scope = GameplayObjectId::FromString("framework.progression.modifiers").High();
    ProgressionModifier explicit_modifier;
    explicit_modifier.id = {GameplayObjectId::FromRaw(modifier_scope, 100)};
    explicit_modifier.target = sid.Value();
    explicit_modifier.operation = ModifierOperation::FinalAdd;
    explicit_modifier.modifier_type = TypeId::FromString("test.explicit.mod");
    auto explicit_id = s.AddModifier(actor, explicit_modifier);
    if (!explicit_id)
        return 26;
    ProgressionModifier duplicate_modifier = explicit_modifier;
    duplicate_modifier.target = sid.Value();
    if (s.AddModifier(actor_b, duplicate_modifier))
        return 27;
    ProgressionModifier generated_modifier;
    generated_modifier.target = sid.Value();
    generated_modifier.operation = ModifierOperation::FinalAdd;
    generated_modifier.modifier_type = TypeId::FromString("test.generated.mod");
    auto generated_id = s.AddModifier(actor_b, generated_modifier);
    if (!generated_id || generated_id.Value().value.Low() <= 100)
        return 28;

    // Restore rejects a generator that can collide with restored own-scope modifier IDs, transactionally.
    auto invalid_snapshot = s.CaptureSnapshot();
    invalid_snapshot.modifier_ids.next = 100;
    ProgressionService invalid_restore_target;
    if (!invalid_restore_target.RegisterAttribute(strength) || !invalid_restore_target.RegisterAttribute(power) ||
        !invalid_restore_target.RegisterTrack(level) || !invalid_restore_target.RegisterPerk(perk) ||
        !invalid_restore_target.Freeze())
        return 29;
    GameplayObjectRef sentinel{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("sentinel")};
    if (!invalid_restore_target.EnsureProfile(sentinel))
        return 30;
    if (invalid_restore_target.RestoreSnapshot(std::move(invalid_snapshot)))
        return 31;
    if (!invalid_restore_target.HasProfile(sentinel))
        return 32;

    // H33: structural milestone references are rejected at Freeze before runtime evaluation.
    ProgressionService invalid_definitions;
    if (!invalid_definitions.RegisterTrack(level))
        return 33;
    MilestoneDefinition invalid_milestone;
    invalid_milestone.canonical_name = "game.milestone.invalid";
    invalid_milestone.track = tid.Value();
    invalid_milestone.threshold_micro = 100;
    invalid_milestone.unlocks.push_back(UnlockDefinitionId::FromString("game.unlock.missing"));
    if (!invalid_definitions.RegisterMilestone(invalid_milestone))
        return 34;
    if (invalid_definitions.Freeze())
        return 35;

    if (!restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 36;
    auto progression_journal_seed = restored.CaptureSnapshot();
    progression_journal_seed.journal.clear();
    progression_journal_seed.next_change_sequence = std::numeric_limits<std::uint64_t>::max();
    if (!restored.RestoreSnapshot(progression_journal_seed))
        return 37;
    if (!restored.SetBaseAttribute(actor, sid.Value(), 11'000'000) ||
        !restored.SetBaseAttribute(actor, sid.Value(), 12'000'000))
        return 38;
    const auto progression_exhausted = restored.CaptureSnapshot();
    if (progression_exhausted.next_change_sequence != 0 || progression_exhausted.journal.size() != 1 ||
        progression_exhausted.journal.front().sequence != std::numeric_limits<std::uint64_t>::max())
        return 39;
    if (!restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 40;
    if (!restored.RestoreSnapshot(progression_exhausted))
        return 41;
    ProgressionService empty_journal;
    if (!empty_journal.ReadChangesSince(empty_journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 42;

    // Goal 4 G4-INFRA-001: every named fallible publication seam starts from a fresh fixture and is full-snapshot atomic.
    const GameplayObjectRef fault_actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("fault-actor")};
    const UnlockRecord fault_unlock{UnlockTypeId::FromString("game.unlock.fault"), TypeId::FromString("game.unlock.value"), {}};
    const auto make_fault_service = [&]() {
        ProgressionService service;
        if (!service.RegisterAttribute(strength) || !service.RegisterTrack(level) || !service.RegisterPerk(perk) || !service.Freeze())
            return ProgressionService{};
        return service;
    };

    {
        auto service = make_fault_service();
        const auto before = service.CaptureSnapshot();
        test_seam::FailNext(test_seam::FaultPoint::EnsureProfileBeforePublish);
        if (service.EnsureProfile(fault_actor) || !SameSnapshot(before, service.CaptureSnapshot()))
            return 980;
    }
    for (const auto point : {test_seam::FaultPoint::SetBaseAttributeBeforePublish,
                             test_seam::FaultPoint::GrantProgressBeforePublish,
                             test_seam::FaultPoint::ReserveProgressBeforePublish,
                             test_seam::FaultPoint::GrantPerkBeforePublish,
                             test_seam::FaultPoint::GrantUnlockBeforePublish,
                             test_seam::FaultPoint::AddModifierBeforePublish})
    {
        auto service = make_fault_service();
        if (!service.EnsureProfile(fault_actor))
            return 981;
        const auto before = service.CaptureSnapshot();
        test_seam::FailNext(point);
        bool failed_as_expected = false;
        switch (point)
        {
        case test_seam::FaultPoint::SetBaseAttributeBeforePublish:
            failed_as_expected = !service.SetBaseAttribute(fault_actor, sid.Value(), 2'000'000);
            break;
        case test_seam::FaultPoint::GrantProgressBeforePublish:
            failed_as_expected = !service.GrantProgress(fault_actor, tid.Value(), 10);
            break;
        case test_seam::FaultPoint::ReserveProgressBeforePublish:
            failed_as_expected = !service.ReserveProgressGrant(fault_actor, tid.Value(), 10);
            break;
        case test_seam::FaultPoint::GrantPerkBeforePublish:
            failed_as_expected = !service.GrantPerk(fault_actor, perkid.Value());
            break;
        case test_seam::FaultPoint::GrantUnlockBeforePublish:
            failed_as_expected = !service.GrantUnlock(fault_actor, fault_unlock);
            break;
        case test_seam::FaultPoint::AddModifierBeforePublish:
        {
            ProgressionModifier modifier;
            modifier.target = sid.Value();
            modifier.operation = ModifierOperation::FinalAdd;
            modifier.modifier_type = TypeId::FromString("test.fault.sweep.modifier");
            failed_as_expected = !service.AddModifier(fault_actor, modifier);
            break;
        }
        default:
            break;
        }
        if (!failed_as_expected || !SameSnapshot(before, service.CaptureSnapshot()))
            return 982;
    }
    {
        auto seeded = make_fault_service();
        if (!seeded.EnsureProfile(fault_actor) || !seeded.SetBaseAttribute(fault_actor, sid.Value(), 3'000'000))
            return 983;
        const auto restore_seed = seeded.CaptureSnapshot();
        for (const auto point : {test_seam::FaultPoint::RestoreCandidateBuild, test_seam::FaultPoint::RestoreBeforeCommit})
        {
            auto service = make_fault_service();
            if (!service.EnsureProfile(fault_actor))
                return 984;
            const auto before = service.CaptureSnapshot();
            auto target = restore_seed;
            test_seam::FailNext(point);
            if (service.RestoreSnapshot(std::move(target)) || !SameSnapshot(before, service.CaptureSnapshot()))
                return 985;
        }
    }

    // G4-PROG-001/002/003: revision exhaustion rejects before destructive mutation or ID consumption.
    ProgressionService atomic;
    auto atomic_strength = atomic.RegisterAttribute(strength);
    auto atomic_track = atomic.RegisterTrack(level);
    auto atomic_perk = atomic.RegisterPerk(perk);
    MilestoneDefinition atomic_milestone;
    atomic_milestone.canonical_name = "game.milestone.atomic";
    atomic_milestone.track = atomic_track.Value();
    atomic_milestone.threshold_micro = 50;
    auto atomic_milestone_id = atomic.RegisterMilestone(atomic_milestone);
    if (!atomic_strength || !atomic_track || !atomic_perk || !atomic_milestone_id || !atomic.Freeze())
        return 941;
    GameplayObjectRef atomic_actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("atomic-actor")};
    if (!atomic.EnsureProfile(atomic_actor))
        return 942;
    auto atomic_seed = atomic.CaptureSnapshot();
    atomic_seed.revision.value = std::numeric_limits<std::uint64_t>::max();
    auto atomic_profile = std::find_if(atomic_seed.profiles.begin(), atomic_seed.profiles.end(),
                                       [&](const auto& profile) { return profile.subject == atomic_actor; });
    if (atomic_profile == atomic_seed.profiles.end())
        return 943;
    atomic_profile->revision.value = std::numeric_limits<std::uint64_t>::max();
    if (!atomic.RestoreSnapshot(atomic_seed))
        return 944;
    const auto before_revision_failure = atomic.CaptureSnapshot();
    if (atomic.RemoveProfile(atomic_actor) || !atomic.HasProfile(atomic_actor))
        return 945;
    if (atomic.SetBaseAttribute(atomic_actor, atomic_strength.Value(), 2'000'000))
        return 946;
    if (atomic.GrantPerk(atomic_actor, atomic_perk.Value()))
        return 947;
    UnlockRecord unlock{UnlockTypeId::FromString("game.unlock.type"), TypeId::FromString("game.unlock.value"), {}};
    if (atomic.GrantUnlock(atomic_actor, unlock))
        return 948;
    ProgressionModifier atomic_modifier;
    atomic_modifier.target = atomic_strength.Value();
    atomic_modifier.operation = ModifierOperation::FinalAdd;
    atomic_modifier.modifier_type = TypeId::FromString("test.atomic.modifier");
    if (atomic.AddModifier(atomic_actor, atomic_modifier))
        return 949;
    const auto after_revision_failure = atomic.CaptureSnapshot();
    if (after_revision_failure.revision != before_revision_failure.revision ||
        after_revision_failure.modifier_ids.next != before_revision_failure.modifier_ids.next ||
        after_revision_failure.profiles.size() != before_revision_failure.profiles.size())
        return 950;

    // A pending progress grant is not consumed when commit cannot publish its required revision.
    auto atomic_reservation = atomic.ReserveProgressGrant(atomic_actor, atomic_track.Value(), 100);
    if (!atomic_reservation)
        return 951;
    atomic.CommitProgressGrant(atomic_reservation.Value());
    auto atomic_track_state = atomic.GetTrack(atomic_actor, atomic_track.Value());
    if (!atomic_track_state || atomic_track_state.Value().progress_micro != 0)
        return 952;
    if (atomic.ReserveProgressGrant(atomic_actor, atomic_track.Value(), 1))
        return 953;
    atomic.ReleaseProgressGrant(atomic_reservation.Value());

    // G4-B06-PROG-004: journal allocation failure after commit rotates the journal epoch without undoing accepted state.
    ProgressionService journal_fault;
    auto jf_strength = journal_fault.RegisterAttribute(strength);
    if (!jf_strength || !journal_fault.Freeze())
        return 957;
    const auto journal_before = journal_fault.CaptureSnapshot();
    test_seam::FailNext(test_seam::FaultPoint::JournalAppend);
    if (!journal_fault.EnsureProfile(atomic_actor))
        return 958;
    const auto journal_after = journal_fault.CaptureSnapshot();
    if (!journal_fault.HasProfile(atomic_actor) || !journal_after.journal.empty() ||
        journal_after.change_epoch == journal_before.change_epoch || journal_after.next_change_sequence != 1)
        return 959;

    // AddModifier allocation publication is failure-atomic and does not consume the staged generator.
    ProgressionService modifier_fault;
    auto mf_strength = modifier_fault.RegisterAttribute(strength);
    if (!mf_strength || !modifier_fault.Freeze() || !modifier_fault.EnsureProfile(atomic_actor))
        return 954;
    const auto modifier_before = modifier_fault.CaptureSnapshot();
    ProgressionModifier fault_modifier;
    fault_modifier.target = mf_strength.Value();
    fault_modifier.operation = ModifierOperation::FinalAdd;
    fault_modifier.modifier_type = TypeId::FromString("test.fault.modifier");
    test_seam::FailNext(test_seam::FaultPoint::AddModifierBeforePublish);
    if (modifier_fault.AddModifier(atomic_actor, fault_modifier))
        return 955;
    const auto modifier_after = modifier_fault.CaptureSnapshot();
    if (modifier_after.modifier_ids.next != modifier_before.modifier_ids.next ||
        modifier_after.revision != modifier_before.revision)
        return 956;

    // G4-PROG-002: revoke paths also preflight global/profile revision exhaustion.
    ProgressionService revoke_guard;
    auto rg_strength = revoke_guard.RegisterAttribute(strength);
    auto rg_perk = revoke_guard.RegisterPerk(perk);
    if (!rg_strength || !rg_perk || !revoke_guard.Freeze() || !revoke_guard.EnsureProfile(atomic_actor) ||
        !revoke_guard.GrantPerk(atomic_actor, rg_perk.Value()))
        return 960;
    UnlockRecord revoke_unlock{UnlockTypeId::FromString("game.unlock.revoke"), TypeId::FromString("game.unlock.revoke.value"), {}};
    if (!revoke_guard.GrantUnlock(atomic_actor, revoke_unlock))
        return 961;
    auto revoke_snapshot = revoke_guard.CaptureSnapshot();
    revoke_snapshot.revision.value = std::numeric_limits<std::uint64_t>::max();
    auto revoke_profile = std::find_if(revoke_snapshot.profiles.begin(), revoke_snapshot.profiles.end(),
                                       [&](const auto& profile) { return profile.subject == atomic_actor; });
    if (revoke_profile == revoke_snapshot.profiles.end())
        return 962;
    revoke_profile->revision.value = std::numeric_limits<std::uint64_t>::max();
    if (!revoke_guard.RestoreSnapshot(revoke_snapshot))
        return 963;
    const auto revoke_before = revoke_guard.CaptureSnapshot();
    if (revoke_guard.RevokePerk(atomic_actor, rg_perk.Value()) ||
        revoke_guard.RevokeUnlock(atomic_actor, revoke_unlock.type, revoke_unlock.value) ||
        !revoke_guard.HasPerk(atomic_actor, rg_perk.Value()) ||
        !revoke_guard.HasUnlock(atomic_actor, revoke_unlock.type, revoke_unlock.value))
        return 964;
    const auto revoke_after = revoke_guard.CaptureSnapshot();
    if (revoke_after.revision != revoke_before.revision || revoke_after.profiles.size() != revoke_before.profiles.size())
        return 965;

    // Full milestone fan-out requires more than one revision. At max-1 it must remain pending and publish nothing.
    ProgressionService fanout_guard;
    auto fg_strength = fanout_guard.RegisterAttribute(strength);
    auto fg_track = fanout_guard.RegisterTrack(level);
    UnlockDefinition reward_definition;
    reward_definition.canonical_name = "game.unlock.fanout.definition";
    reward_definition.type = UnlockTypeId::FromString("game.unlock.fanout.type");
    reward_definition.value = TypeId::FromString("game.unlock.fanout.value");
    auto fg_unlock = fanout_guard.RegisterUnlockDefinition(reward_definition);
    MilestoneDefinition fanout_milestone;
    fanout_milestone.canonical_name = "game.milestone.fanout";
    fanout_milestone.track = fg_track.Value();
    fanout_milestone.threshold_micro = 50;
    if (fg_unlock)
        fanout_milestone.unlocks.push_back(fg_unlock.Value());
    auto fg_milestone = fanout_guard.RegisterMilestone(fanout_milestone);
    if (!fg_strength || !fg_track || !fg_unlock || !fg_milestone || !fanout_guard.Freeze() ||
        !fanout_guard.EnsureProfile(atomic_actor))
        return 966;
    auto fanout_seed = fanout_guard.CaptureSnapshot();
    fanout_seed.revision.value = std::numeric_limits<std::uint64_t>::max() - 1;
    auto fanout_profile = std::find_if(fanout_seed.profiles.begin(), fanout_seed.profiles.end(),
                                       [&](const auto& profile) { return profile.subject == atomic_actor; });
    if (fanout_profile == fanout_seed.profiles.end())
        return 967;
    fanout_profile->revision.value = std::numeric_limits<std::uint64_t>::max() - 1;
    if (!fanout_guard.RestoreSnapshot(fanout_seed))
        return 968;
    auto fanout_reservation = fanout_guard.ReserveProgressGrant(atomic_actor, fg_track.Value(), 100);
    if (!fanout_reservation)
        return 969;
    const auto fanout_before = fanout_guard.CaptureSnapshot();
    fanout_guard.CommitProgressGrant(fanout_reservation.Value());
    const auto fanout_after = fanout_guard.CaptureSnapshot();
    auto fanout_track_state = fanout_guard.GetTrack(atomic_actor, fg_track.Value());
    if (!fanout_track_state || fanout_track_state.Value().progress_micro != 0 ||
        fanout_guard.HasUnlock(atomic_actor, reward_definition.type, reward_definition.value) ||
        fanout_after.revision != fanout_before.revision)
        return 970;
    if (fanout_guard.ReserveProgressGrant(atomic_actor, fg_track.Value(), 1))
        return 971;
    fanout_guard.ReleaseProgressGrant(fanout_reservation.Value());


    // Goal 4 exact public-API evidence: default/invalid progression surface.
    ProgressionService api_evidence;
    if (api_evidence.IsFrozen())
        return 1100;
    if (!ProgressionService::Domain().IsValid())
        return 1101;
    if (!api_evidence.QueryPrerequisites(GameplayObjectRef{}, {}).satisfied)
        return 1102;
    if (api_evidence.GetDiagnostics().profiles != 0)
        return 1103;
    if (api_evidence.EvaluateMilestones(GameplayObjectRef{}))
        return 1104;
    if (api_evidence.RemoveModifier(GameplayObjectRef{}, ProgressionModifierId{}))
        return 1105;
    if (api_evidence.SetProgress(GameplayObjectRef{}, ProgressionTrackId{}, 0))
        return 1106;
    if (api_evidence.ReplaceModifiersBySource(GameplayObjectRef{}, GameplayObjectRef{}, {}))
        return 1107;
    if (api_evidence.RevokeUnlockFromSource(GameplayObjectRef{}, UnlockTypeId{}, TypeId{}, GameplayObjectRef{}))
        return 1108;
    if (api_evidence.RemoveModifiersBySource(GameplayObjectRef{}, GameplayObjectRef{}) != 0)
        return 1109;
    if (api_evidence.GetProfileSnapshot(GameplayObjectRef{}))
        return 1110;

    return 0;
}
