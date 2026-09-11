#include "allocation_fault_injection.h"
#include "Epidemic/GameFramework/Society/society.h"

#include <cstdint>
#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::society;

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

    // Milestone 2: RestoreSnapshot preserves live state at allocation boundaries.
    const auto allocation_before = restored.CaptureSnapshot();
    bool saw_restore_allocation_failure = false;
    for (long long fail_after = 0; fail_after < 32; ++fail_after)
    {
        auto allocation_target = allocation_before;
        bool failed = false;
        try
        {
            epidemic::tests::allocation_fault::FailAfter fault(fail_after);
            const auto restored_under_fault = restored.RestoreSnapshot(std::move(allocation_target));
            failed = !restored_under_fault;
        }
        catch (const std::bad_alloc &)
        {
            failed = true;
        }
        if (!failed)
            break;
        saw_restore_allocation_failure = true;
        if (restored.CaptureSnapshot().revision != allocation_before.revision)
            return 947;
    }
    if (!saw_restore_allocation_failure)
        return 948;
    return 0;
}
