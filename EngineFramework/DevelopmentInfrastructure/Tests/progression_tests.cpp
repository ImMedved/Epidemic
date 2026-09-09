#include "Epidemic/GameFramework/Progression/progression.h"
#include <limits>
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::progression;
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

    return 0;
}
