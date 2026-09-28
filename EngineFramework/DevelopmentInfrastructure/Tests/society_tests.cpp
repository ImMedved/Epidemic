#include "Epidemic/GameFramework/Society/society.h"

#include <cstdint>
#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::society;

namespace epidemic::gameplay::society::testing
{
void FailNextLocalAllocationForTest() noexcept;
}

static GameplayObjectRef Ref(const char *name)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(name)};
}

static RelationshipTypeDefinition TrustDefinition(RelationshipTypeId id)
{
    RelationshipTypeDefinition definition;
    definition.id = id;
    definition.minimum_value_micro = -1'000'000;
    definition.maximum_value_micro = 1'000'000;
    definition.default_value_micro = 0;
    definition.state_thresholds = {{-1'000'000, RelationshipState::Hostile},
                                   {-300'000, RelationshipState::Neutral},
                                   {300'001, RelationshipState::Friendly}};
    definition.direct_attitude_weight_micro = 1'000'000;
    definition.group_attitude_weight_micro = 500'000;
    return definition;
}


static bool SameMembership(const MembershipRecord &a, const MembershipRecord &b)
{
    return a.id == b.id && a.member == b.member && a.group == b.group && a.role == b.role && a.rank == b.rank &&
           a.state == b.state && a.joined_at == b.joined_at && a.revision == b.revision;
}

static bool SameRelationship(const RelationshipRecord &a, const RelationshipRecord &b)
{
    return a.id == b.id && a.subject == b.subject && a.target == b.target && a.type == b.type &&
           a.value_micro == b.value_micro && a.state == b.state && a.updated_at == b.updated_at &&
           a.revision == b.revision;
}

static bool SameReputation(const ReputationRecord &a, const ReputationRecord &b)
{
    return a.subject == b.subject && a.scope == b.scope && a.track == b.track && a.value_micro == b.value_micro &&
           a.revision == b.revision;
}

static bool SameChange(const SocietyChange &a, const SocietyChange &b)
{
    return a.sequence == b.sequence && a.kind == b.kind && a.subject == b.subject && a.target == b.target &&
           a.relationship_type == b.relationship_type && a.reputation_track == b.reputation_track &&
           a.membership == b.membership && a.context == b.context && a.revision == b.revision;
}

template <typename T, typename Equal>
static bool SameVector(const std::vector<T> &a, const std::vector<T> &b, Equal equal)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (!equal(a[i], b[i]))
            return false;
    }
    return true;
}

static bool SameMutableSnapshot(const SocietySnapshot &a, const SocietySnapshot &b)
{
    return a.definitions_frozen == b.definitions_frozen && a.groups.size() == b.groups.size() &&
           a.relationship_types.size() == b.relationship_types.size() && a.reputation_tracks.size() == b.reputation_tracks.size() &&
           SameVector(a.memberships, b.memberships, SameMembership) &&
           SameVector(a.relationships, b.relationships, SameRelationship) &&
           SameVector(a.reputations, b.reputations, SameReputation) &&
           a.membership_ids.next == b.membership_ids.next && a.relationship_ids.next == b.relationship_ids.next &&
           a.revision == b.revision && SameVector(a.journal, b.journal, SameChange) &&
           a.next_change_sequence == b.next_change_sequence && a.change_epoch == b.change_epoch;
}

static ReputationTrackDefinition CrimeReputationDefinition(ReputationTrackId id)
{
    ReputationTrackDefinition definition;
    definition.id = id;
    definition.minimum_value_micro = -1'000'000;
    definition.maximum_value_micro = 1'000'000;
    definition.default_value_micro = 0;
    definition.attitude_weight_micro = 250'000;
    definition.standing_thresholds = {{-1'000'000, TagId::FromString("standing.outlaw")},
                                      {0, TagId::FromString("standing.neutral")},
                                      {500'000, TagId::FromString("standing.respected")}};
    return definition;
}

int main()
{
    SocietyService society;
    const auto player = Ref("player");
    const auto npc = Ref("npc");
    const auto guards = Ref("guards");
    const auto missing_group = Ref("missing_group");
    const auto guard_role = SocialRoleId::FromString("role.guard");
    const auto captain_role = SocialRoleId::FromString("role.captain");
    const auto trust = RelationshipTypeId::FromString("rel.trust");
    const auto crime_rep = ReputationTrackId::FromString("rep.crime");

    SocialGroupDefinition group;
    group.group = guards;
    group.type = SocialGroupTypeId::FromString("group.faction");
    if (!society.RegisterGroup(group))
        return 1;
    if (!society.RegisterRelationshipType(TrustDefinition(trust)))
        return 2;
    if (!society.RegisterReputationTrack(CrimeReputationDefinition(crime_rep)))
        return 3;

    MembershipRecord before_freeze;
    before_freeze.member = npc;
    before_freeze.group = guards;
    if (society.AddMembership(before_freeze))
        return 4;

    if (!society.FreezeDefinitions())
        return 5;
    if (society.RegisterGroup({Ref("late_group"), {}, SocialGroupTypeId::FromString("group.faction"), {}, {}}))
        return 6;

    MembershipRecord unknown_group;
    unknown_group.member = npc;
    unknown_group.group = missing_group;
    if (society.AddMembership(unknown_group))
        return 7;

    MembershipRecord membership;
    membership.member = npc;
    membership.group = guards;
    membership.role = guard_role;
    membership.rank = 3;
    membership.state = MembershipState::Active;
    auto membership_result = society.AddMembership(membership);
    if (!membership_result)
        return 8;
    const auto membership_id = membership_result.Value();

    auto duplicate_retry = society.AddMembership(membership);
    if (!duplicate_retry || duplicate_retry.Value() != membership_id)
        return 9;
    membership.role = captain_role;
    if (society.AddMembership(membership))
        return 10;

    if (!society.HasRole(npc, guard_role, guards))
        return 11;
    if (!society.SetMembershipState(membership_id, MembershipState::Suspended))
        return 12;
    if (society.HasRole(npc, guard_role, guards))
        return 13;
    if (!society.SetMembershipState(membership_id, MembershipState::Active))
        return 14;
    if (!society.SetMembershipRole(membership_id, captain_role))
        return 15;
    if (society.HasRole(npc, guard_role, guards) || !society.HasRole(npc, captain_role, guards))
        return 16;

    GameplayContext time_context;
    time_context.time = GameplayTimePoint{100};
    if (!society.ApplySocialChange({npc, player, trust, -500'000, TypeId::FromString("reason.insult"), time_context}))
        return 17;
    auto relationship = society.GetRelationship(npc, player, trust);
    if (!relationship || relationship->state != RelationshipState::Hostile || relationship->value_micro != -500'000 ||
        relationship->updated_at.ticks != 100)
        return 18;

    RelationshipRecord conflicting_id = *relationship;
    conflicting_id.id = RelationshipId::FromString("different.relationship.id");
    conflicting_id.value_micro = 10;
    if (society.SetRelationship(conflicting_id))
        return 19;

    if (!society.ApplySocialChange({npc, player, trust, std::numeric_limits<std::int64_t>::min(), {}, time_context}))
        return 20;
    relationship = society.GetRelationship(npc, player, trust);
    if (!relationship || relationship->value_micro != -1'000'000)
        return 21;

    RelationshipRecord group_attitude;
    group_attitude.subject = guards;
    group_attitude.target = player;
    group_attitude.type = trust;
    group_attitude.value_micro = -200'000;
    if (!society.SetRelationship(group_attitude, time_context))
        return 22;

    ReputationRecord reputation;
    reputation.subject = player;
    reputation.scope = guards;
    reputation.track = crime_rep;
    reputation.value_micro = -200'000;
    if (!society.SetReputation(reputation, time_context))
        return 23;
    reputation.value_micro = -1'000'001;
    if (society.SetReputation(reputation, time_context))
        return 24;
    if (!society.ApplyReputationChange({player, guards, crime_rep, std::numeric_limits<std::int64_t>::min(), {}, time_context}))
        return 25;
    const auto bounded_reputation = society.GetReputation(player, guards, crime_rep);
    if (!bounded_reputation || bounded_reputation->value_micro != -1'000'000)
        return 26;

    const auto standing = society.GetSocialStanding(player, guards);
    if (standing.reputations.size() != 1 || !standing.standing_tags.HasExact(TagId::FromString("standing.outlaw")))
        return 27;

    const auto attitude = society.GetEffectiveAttitude({npc, player, time_context});
    // -1,000,000 direct + (-200,000 * 0.5 group) + (-1,000,000 * 0.25 reputation).
    if (attitude != -1'350'000)
        return 28;

    const auto stable_snapshot = society.CaptureSnapshot();
    auto corrupted = stable_snapshot;
    corrupted.memberships.front().group = missing_group;
    if (society.RestoreSnapshot(std::move(corrupted)))
        return 29;
    if (!society.GetMembership(membership_id) || !society.GetRelationship(npc, player, trust))
        return 30;

    auto stale_generator = stable_snapshot;
    stale_generator.membership_ids.next = membership_id.value.Low();
    if (society.RestoreSnapshot(std::move(stale_generator)))
        return 31;

    SocietyService restored;
    if (!restored.RestoreSnapshot(stable_snapshot))
        return 32;
    if (!restored.HasRole(npc, captain_role, guards))
        return 33;
    if (!restored.GetRelationship(npc, player, trust) ||
        !restored.GetSocialStanding(player, guards).standing_tags.HasExact(TagId::FromString("standing.outlaw")))
        return 34;
    if (restored.GetEffectiveAttitude({npc, player, time_context}) != -1'350'000)
        return 35;

    // Goal 4 G4-SOC-001: every revision-bearing mutation rejects UINT64_MAX without state drift.
    auto max_revision_snapshot = stable_snapshot;
    max_revision_snapshot.revision.value = std::numeric_limits<std::uint64_t>::max();
    const auto max_revision_atomic = [&](auto &&mutation) {
        SocietyService candidate;
        if (!candidate.RestoreSnapshot(max_revision_snapshot))
            return false;
        const auto before = candidate.CaptureSnapshot();
        if (mutation(candidate))
            return false;
        const auto after = candidate.CaptureSnapshot();
        return SameMutableSnapshot(before, after);
    };
    if (!max_revision_atomic([&](SocietyService &candidate) {
            return static_cast<bool>(candidate.SetMembershipRole(membership_id, guard_role, time_context));
        }))
        return 39;
    if (!max_revision_atomic([&](SocietyService &candidate) {
            return static_cast<bool>(candidate.RemoveMembership(membership_id, time_context));
        }))
        return 40;
    if (!max_revision_atomic([&](SocietyService &candidate) {
            RelationshipRecord update = *candidate.GetRelationship(npc, player, trust);
            update.value_micro = -900'000;
            return static_cast<bool>(candidate.SetRelationship(update, time_context));
        }))
        return 41;
    if (!max_revision_atomic([&](SocietyService &candidate) {
            RelationshipRecord fresh;
            fresh.subject = Ref("fresh.subject");
            fresh.target = Ref("fresh.target");
            fresh.type = trust;
            fresh.value_micro = 1;
            return static_cast<bool>(candidate.SetRelationship(fresh, time_context));
        }))
        return 42;
    if (!max_revision_atomic([&](SocietyService &candidate) {
            return static_cast<bool>(candidate.ApplySocialChange({npc, player, trust, 1, {}, time_context}));
        }))
        return 43;
    if (!max_revision_atomic([&](SocietyService &candidate) {
            return static_cast<bool>(candidate.SetReputation({player, guards, crime_rep, -999'999, {}}, time_context));
        }))
        return 44;

    // Goal 4 G4-SOC-002/G4-INFRA-001: narrow Society-local publication failures are atomic.
    {
        SocietyService candidate;
        if (!candidate.RestoreSnapshot(stable_snapshot))
            return 45;
        const auto before = candidate.CaptureSnapshot();
        MembershipRecord injected;
        injected.member = Ref("allocation.member");
        injected.group = guards;
        const auto before_groups = candidate.FindGroupsOf(injected.member);
        const auto before_members = candidate.FindMembersOf(guards);
        epidemic::gameplay::society::testing::FailNextLocalAllocationForTest();
        if (candidate.AddMembership(injected))
            return 46;
        const auto after = candidate.CaptureSnapshot();
        if (!SameMutableSnapshot(before, after) || candidate.FindGroupsOf(injected.member).size() != before_groups.size() ||
            candidate.FindMembersOf(guards).size() != before_members.size())
            return 47;
    }
    {
        SocietyService candidate;
        if (!candidate.RestoreSnapshot(stable_snapshot))
            return 48;
        const auto before = candidate.CaptureSnapshot();
        RelationshipRecord injected;
        injected.subject = Ref("allocation.relationship.subject");
        injected.target = Ref("allocation.relationship.target");
        injected.type = trust;
        epidemic::gameplay::society::testing::FailNextLocalAllocationForTest();
        if (candidate.SetRelationship(injected, time_context))
            return 49;
        const auto after = candidate.CaptureSnapshot();
        if (!SameMutableSnapshot(before, after) ||
            candidate.GetRelationship(injected.subject, injected.target, injected.type).has_value())
            return 50;
    }
    {
        SocietyService candidate;
        if (!candidate.RestoreSnapshot(stable_snapshot))
            return 51;
        const auto before = candidate.CaptureSnapshot();
        ReputationRecord injected;
        injected.subject = Ref("allocation.reputation.subject");
        injected.scope = guards;
        injected.track = crime_rep;
        epidemic::gameplay::society::testing::FailNextLocalAllocationForTest();
        if (candidate.SetReputation(injected, time_context))
            return 52;
        const auto after = candidate.CaptureSnapshot();
        if (!SameMutableSnapshot(before, after) || candidate.GetReputation(injected.subject, injected.scope, injected.track).has_value())
            return 53;
    }

    // Exercise bounded journal and explicit gap reporting.
    for (std::uint64_t i = 0; i < 4200; ++i)
    {
        const std::int64_t value = (i & 1u) == 0 ? -999'999 : -999'998;
        if (!restored.SetReputation({player, guards, crime_rep, value, {}}, time_context))
            return 36;
    }
    const auto old_reader = restored.ReadChangesSince(ChangeCursor{});
    if (!old_reader.snapshot_required || old_reader.oldest_available_sequence <= 1)
        return 37;
    const auto latest = restored.ReadChangesSince(old_reader.latest_cursor);
    if (latest.snapshot_required || !latest.changes.empty())
        return 38;

    // Goal 4: module-local failure seam proves RestoreSnapshot pre-state atomicity.
    const auto allocation_before = restored.CaptureSnapshot();
    epidemic::gameplay::society::testing::FailNextLocalAllocationForTest();
    const auto injected_restore = restored.RestoreSnapshot(allocation_before);
    if (injected_restore)
        return 947;
    const auto allocation_after = restored.CaptureSnapshot();
    if (allocation_after.revision != allocation_before.revision ||
        allocation_after.change_epoch != allocation_before.change_epoch)
        return 948;
    return 0;
}
