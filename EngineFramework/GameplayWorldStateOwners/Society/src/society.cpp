#include "Epidemic/GameFramework/Society/society.h"
#include "Epidemic/Foundation/error.h"

#include <iterator>
#include <limits>
#include <utility>

namespace epidemic::gameplay::society
{
namespace
{
constexpr std::int64_t kMicroOne = 1'000'000;

foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] std::size_t HashCombine(std::size_t a, std::size_t b) noexcept
{
    return a ^ (b + static_cast<std::size_t>(0x9E3779B97F4A7C15ull) + (a << 6u) + (a >> 2u));
}

[[nodiscard]] std::int64_t SaturatingAdd(std::int64_t a, std::int64_t b) noexcept
{
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
        return std::numeric_limits<std::int64_t>::max();
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)
        return std::numeric_limits<std::int64_t>::min();
    return a + b;
}

[[nodiscard]] std::int64_t SaturatingMultiply(std::int64_t a, std::int64_t b) noexcept
{
    if (a == 0 || b == 0)
        return 0;
    if (a == -1 && b == std::numeric_limits<std::int64_t>::min())
        return std::numeric_limits<std::int64_t>::max();
    if (b == -1 && a == std::numeric_limits<std::int64_t>::min())
        return std::numeric_limits<std::int64_t>::max();
    if (a > 0)
    {
        if (b > 0 && a > std::numeric_limits<std::int64_t>::max() / b)
            return std::numeric_limits<std::int64_t>::max();
        if (b < 0 && b < std::numeric_limits<std::int64_t>::min() / a)
            return std::numeric_limits<std::int64_t>::min();
    }
    else
    {
        if (b > 0 && a < std::numeric_limits<std::int64_t>::min() / b)
            return std::numeric_limits<std::int64_t>::min();
        if (b < 0 && a < std::numeric_limits<std::int64_t>::max() / b)
            return std::numeric_limits<std::int64_t>::max();
    }
    return a * b;
}

[[nodiscard]] std::int64_t ScaleMicro(std::int64_t value, std::int64_t weight_micro) noexcept
{
    const auto whole = value / kMicroOne;
    const auto remainder = value % kMicroOne;
    const auto whole_part = SaturatingMultiply(whole, weight_micro);
    // Definition validation restricts weight to [-1e6, 1e6], so this multiplication is bounded by 1e12.
    const auto remainder_part = (remainder * weight_micro) / kMicroOne;
    return SaturatingAdd(whole_part, remainder_part);
}

template <class WrappedId>
void AdvanceGeneratorPast(MonotonicIdGenerator<GameplayObjectId> &generator, WrappedId id) noexcept
{
    if (!id.IsValid() || id.value.High() != generator.Scope().Raw())
        return;
    auto snapshot = generator.GetSnapshot();
    if (snapshot.next == 0 || id.value.Low() < snapshot.next)
        return;
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}

[[nodiscard]] bool MembershipContributesToGroupAttitude(MembershipState state) noexcept
{
    return state == MembershipState::Active || state == MembershipState::Honorary;
}

template <class T>
void EraseId(std::vector<T> &values, const T &value)
{
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

[[nodiscard]] bool IsValidMembershipState(MembershipState state) noexcept
{
    switch (state)
    {
    case MembershipState::Active:
    case MembershipState::Suspended:
    case MembershipState::Former:
    case MembershipState::Banned:
    case MembershipState::Applicant:
    case MembershipState::Honorary:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidRelationshipState(RelationshipState state) noexcept
{
    switch (state)
    {
    case RelationshipState::Neutral:
    case RelationshipState::Friendly:
    case RelationshipState::Hostile:
    case RelationshipState::Allied:
    case RelationshipState::Feared:
    case RelationshipState::Trusted:
    case RelationshipState::Unknown:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsAllowedMembershipTransition(MembershipState from, MembershipState to) noexcept
{
    if (!IsValidMembershipState(from) || !IsValidMembershipState(to))
        return false;
    if (from == to)
        return true;
    switch (from)
    {
    case MembershipState::Applicant:
        return to == MembershipState::Active || to == MembershipState::Former || to == MembershipState::Banned;
    case MembershipState::Active:
    case MembershipState::Honorary:
        return to == MembershipState::Suspended || to == MembershipState::Former || to == MembershipState::Banned;
    case MembershipState::Suspended:
        return to == MembershipState::Active || to == MembershipState::Former || to == MembershipState::Banned;
    case MembershipState::Former:
    case MembershipState::Banned:
        return false;
    }
    return false;
}

} // namespace

std::size_t SocietyService::MembershipKeyHash::operator()(const MembershipKey &key) const noexcept
{
    return HashCombine(std::hash<GameplayObjectRef>{}(key.member), std::hash<GameplayObjectRef>{}(key.group));
}

std::size_t SocietyService::RelationshipKeyHash::operator()(const RelationshipKey &key) const noexcept
{
    auto h = HashCombine(std::hash<GameplayObjectRef>{}(key.subject), std::hash<GameplayObjectRef>{}(key.target));
    return HashCombine(h, std::hash<TypeId>{}(key.type.value));
}

std::size_t SocietyService::ReputationKeyHash::operator()(const ReputationKey &key) const noexcept
{
    auto h = HashCombine(std::hash<GameplayObjectRef>{}(key.subject), std::hash<GameplayObjectRef>{}(key.scope));
    return HashCombine(h, std::hash<TypeId>{}(key.track.value));
}

foundation::Result<void> SocietyService::RegisterGroup(SocialGroupDefinition group)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.definitions_frozen", "society definitions are frozen"));
    if (!group.group.IsValid() || !group.type.IsValid() || groups_.contains(group.group))
        return foundation::Result<void>::Failure(Error("gameplay.society.invalid_group", "invalid or duplicate group"));
    if (!Bump())
        return foundation::Result<void>::Failure(Error("gameplay.society.revision_exhausted", "society revision exhausted"));
    group.revision = revision_;
    const auto group_ref = group.group;
    groups_.emplace(group_ref, std::move(group));
    Record({0, SocietyChangeKind::GroupCreated, group_ref, {}, {}, {}, {}, {}, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocietyService::RegisterRelationshipType(RelationshipTypeDefinition definition)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.definitions_frozen", "society definitions are frozen"));
    if (!definition.id.IsValid() || relationship_types_.contains(definition.id) ||
        definition.minimum_value_micro > definition.maximum_value_micro ||
        definition.default_value_micro < definition.minimum_value_micro ||
        definition.default_value_micro > definition.maximum_value_micro ||
        definition.direct_attitude_weight_micro < -kMicroOne || definition.direct_attitude_weight_micro > kMicroOne ||
        definition.group_attitude_weight_micro < -kMicroOne || definition.group_attitude_weight_micro > kMicroOne ||
        definition.state_thresholds.empty())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.society.invalid_relationship_definition", "invalid relationship type definition"));
    }
    std::sort(definition.state_thresholds.begin(), definition.state_thresholds.end(), [](const auto &a, const auto &b) {
        return a.minimum_value_micro < b.minimum_value_micro;
    });
    if (definition.state_thresholds.front().minimum_value_micro > definition.minimum_value_micro)
        return foundation::Result<void>::Failure(Error("gameplay.society.relationship_threshold_gap",
                                                       "relationship thresholds do not cover minimum value"));
    for (std::size_t i = 0; i < definition.state_thresholds.size(); ++i)
    {
        const auto threshold = definition.state_thresholds[i].minimum_value_micro;
        if (!IsValidRelationshipState(definition.state_thresholds[i].state) ||
            threshold < definition.minimum_value_micro || threshold > definition.maximum_value_micro ||
            (i > 0 && threshold == definition.state_thresholds[i - 1].minimum_value_micro))
        {
            return foundation::Result<void>::Failure(Error("gameplay.society.invalid_relationship_threshold",
                                                           "invalid or duplicate relationship threshold"));
        }
    }
    if (!Bump())
        return foundation::Result<void>::Failure(Error("gameplay.society.revision_exhausted", "society revision exhausted"));
    definition.revision = revision_;
    relationship_types_.emplace(definition.id, std::move(definition));
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocietyService::RegisterReputationTrack(ReputationTrackDefinition definition)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.definitions_frozen", "society definitions are frozen"));
    if (!definition.id.IsValid() || reputation_tracks_.contains(definition.id) ||
        definition.minimum_value_micro > definition.maximum_value_micro ||
        definition.default_value_micro < definition.minimum_value_micro ||
        definition.default_value_micro > definition.maximum_value_micro ||
        definition.attitude_weight_micro < -kMicroOne || definition.attitude_weight_micro > kMicroOne)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.society.invalid_reputation_definition", "invalid reputation track definition"));
    }
    std::sort(definition.standing_thresholds.begin(), definition.standing_thresholds.end(), [](const auto &a, const auto &b) {
        return a.minimum_value_micro < b.minimum_value_micro;
    });
    for (std::size_t i = 0; i < definition.standing_thresholds.size(); ++i)
    {
        const auto &threshold = definition.standing_thresholds[i];
        if (!threshold.standing_tag.IsValid() || threshold.minimum_value_micro < definition.minimum_value_micro ||
            threshold.minimum_value_micro > definition.maximum_value_micro ||
            (i > 0 && threshold.minimum_value_micro == definition.standing_thresholds[i - 1].minimum_value_micro))
        {
            return foundation::Result<void>::Failure(Error("gameplay.society.invalid_reputation_threshold",
                                                           "invalid or duplicate reputation standing threshold"));
        }
    }
    if (!Bump())
        return foundation::Result<void>::Failure(Error("gameplay.society.revision_exhausted", "society revision exhausted"));
    definition.revision = revision_;
    reputation_tracks_.emplace(definition.id, std::move(definition));
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocietyService::FreezeDefinitions()
{
    definitions_frozen_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<MembershipId> SocietyService::AddMembership(MembershipRecord record)
{
    if (!definitions_frozen_)
        return foundation::Result<MembershipId>::Failure(
            Error("gameplay.society.definitions_not_frozen", "freeze society definitions before runtime membership"));
    if (!record.member.IsValid() || !record.group.IsValid() || !groups_.contains(record.group) ||
        !IsValidMembershipState(record.state))
        return foundation::Result<MembershipId>::Failure(
            Error("gameplay.society.invalid_membership", "invalid membership or unknown group"));

    const MembershipKey key{record.member, record.group};
    if (const auto existing_it = membership_by_key_.find(key); existing_it != membership_by_key_.end())
    {
        const auto *existing = GetMembership(existing_it->second);
        if (existing && (!record.id.IsValid() || record.id == existing->id) && record.role == existing->role &&
            record.rank == existing->rank && record.state == existing->state)
        {
            return foundation::Result<MembershipId>::Success(existing->id);
        }
        return foundation::Result<MembershipId>::Failure(
            Error("gameplay.society.membership_conflict", "membership already exists for member and group"));
    }

    if (record.id.IsValid() && memberships_.contains(record.id))
        return foundation::Result<MembershipId>::Failure(
            Error("gameplay.society.duplicate_membership", "duplicate membership id"));
    if (!record.id.IsValid())
    {
        record.id = MembershipId{membership_ids_.Next()};
        if (!record.id.IsValid())
            return foundation::Result<MembershipId>::Failure(
                Error("gameplay.society.membership_id_exhausted", "membership id generator exhausted"));
    }
    else
    {
        AdvanceGeneratorPast(membership_ids_, record.id);
    }
    if (memberships_.contains(record.id))
        return foundation::Result<MembershipId>::Failure(
            Error("gameplay.society.duplicate_membership", "duplicate membership id"));

    if (!Bump())
        return foundation::Result<MembershipId>::Failure(Error("gameplay.society.revision_exhausted", "society revision exhausted"));
    record.revision = revision_;
    const auto id = record.id;
    memberships_.emplace(id, record);
    IndexMembership(record);
    Record({0, SocietyChangeKind::MembershipAdded, record.member, record.group, {}, {}, id, {}, revision_});
    return foundation::Result<MembershipId>::Success(id);
}

foundation::Result<void> SocietyService::SetMembershipState(MembershipId id, MembershipState state, GameplayContext context)
{
    auto it = memberships_.find(id);
    if (it == memberships_.end())
        return foundation::Result<void>::Failure(Error("gameplay.society.membership_missing", "membership missing"));
    if (!IsValidMembershipState(state) || !IsAllowedMembershipTransition(it->second.state, state))
        return foundation::Result<void>::Failure(Error("gameplay.society.invalid_membership_transition", "invalid membership state transition"));
    if (it->second.state == state)
        return foundation::Result<void>::Success();
    if (!Bump())
        return foundation::Result<void>::Failure(Error("gameplay.society.revision_exhausted", "society revision exhausted"));
    it->second.state = state;
    it->second.revision = revision_;
    Record({0, SocietyChangeKind::MembershipChanged, it->second.member, it->second.group, {}, {}, id, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocietyService::SetMembershipRole(MembershipId id, SocialRoleId role, GameplayContext context)
{
    auto it = memberships_.find(id);
    if (it == memberships_.end())
        return foundation::Result<void>::Failure(Error("gameplay.society.membership_missing", "membership missing"));
    if (it->second.role == role)
        return foundation::Result<void>::Success();
    const auto old_role = it->second.role;
    Bump();
    it->second.role = role;
    it->second.revision = revision_;
    if (old_role.IsValid())
        Record({0, SocietyChangeKind::SocialRoleRemoved, it->second.member, it->second.group, {}, {}, id, context, revision_});
    if (role.IsValid())
        Record({0, SocietyChangeKind::SocialRoleAssigned, it->second.member, it->second.group, {}, {}, id, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocietyService::RemoveMembership(MembershipId id, GameplayContext context)
{
    auto it = memberships_.find(id);
    if (it == memberships_.end())
        return foundation::Result<void>::Failure(Error("gameplay.society.membership_missing", "membership missing"));
    const auto record = it->second;
    UnindexMembership(record);
    memberships_.erase(it);
    Bump();
    Record({0, SocietyChangeKind::MembershipRemoved, record.member, record.group, {}, {}, id, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<RelationshipId> SocietyService::SetRelationship(RelationshipRecord record, GameplayContext context)
{
    if (!definitions_frozen_)
        return foundation::Result<RelationshipId>::Failure(
            Error("gameplay.society.definitions_not_frozen", "freeze society definitions before runtime relationships"));
    if (!record.subject.IsValid() || !record.target.IsValid() || !record.type.IsValid())
        return foundation::Result<RelationshipId>::Failure(
            Error("gameplay.society.invalid_relationship", "invalid relationship"));
    const auto definition_it = relationship_types_.find(record.type);
    if (definition_it == relationship_types_.end())
        return foundation::Result<RelationshipId>::Failure(
            Error("gameplay.society.relationship_type_missing", "relationship type definition missing"));
    const auto &definition = definition_it->second;
    if (record.value_micro < definition.minimum_value_micro || record.value_micro > definition.maximum_value_micro)
        return foundation::Result<RelationshipId>::Failure(
            Error("gameplay.society.relationship_value_out_of_range", "relationship value outside definition range"));

    const RelationshipKey key{record.subject, record.target, record.type};
    if (const auto existing_it = relationship_by_key_.find(key); existing_it != relationship_by_key_.end())
    {
        auto &existing = relationships_.at(existing_it->second);
        if (record.id.IsValid() && record.id != existing.id)
            return foundation::Result<RelationshipId>::Failure(
                Error("gameplay.society.relationship_key_conflict", "relationship semantic key already has another id"));
        Bump();
        existing.value_micro = record.value_micro;
        existing.state = ResolveRelationshipState(record.type, record.value_micro);
        existing.updated_at = record.updated_at.ticks != 0 ? record.updated_at : context.time;
        existing.revision = revision_;
        ++diagnostics_.relationship_changes;
        Record({0, SocietyChangeKind::RelationshipChanged, existing.subject, existing.target, existing.type, {}, {}, context,
                revision_});
        return foundation::Result<RelationshipId>::Success(existing.id);
    }

    if (record.id.IsValid() && relationships_.contains(record.id))
        return foundation::Result<RelationshipId>::Failure(
            Error("gameplay.society.duplicate_relationship", "duplicate relationship id"));
    if (!record.id.IsValid())
    {
        record.id = RelationshipId{relationship_ids_.Next()};
        if (!record.id.IsValid())
            return foundation::Result<RelationshipId>::Failure(
                Error("gameplay.society.relationship_id_exhausted", "relationship id generator exhausted"));
    }
    else
    {
        AdvanceGeneratorPast(relationship_ids_, record.id);
    }
    if (relationships_.contains(record.id))
        return foundation::Result<RelationshipId>::Failure(
            Error("gameplay.society.duplicate_relationship", "duplicate relationship id"));

    Bump();
    record.state = ResolveRelationshipState(record.type, record.value_micro);
    if (record.updated_at.ticks == 0)
        record.updated_at = context.time;
    record.revision = revision_;
    const auto id = record.id;
    relationships_.emplace(id, record);
    IndexRelationship(record);
    ++diagnostics_.relationship_changes;
    Record({0, SocietyChangeKind::RelationshipChanged, record.subject, record.target, record.type, {}, {}, context,
            revision_});
    return foundation::Result<RelationshipId>::Success(id);
}

RelationshipRecord *SocietyService::FindMutableRelationship(GameplayObjectRef subject, GameplayObjectRef target,
                                                            RelationshipTypeId type) noexcept
{
    const auto key_it = relationship_by_key_.find(RelationshipKey{subject, target, type});
    if (key_it == relationship_by_key_.end())
        return nullptr;
    const auto it = relationships_.find(key_it->second);
    return it == relationships_.end() ? nullptr : &it->second;
}

RelationshipState SocietyService::ResolveRelationshipState(RelationshipTypeId type, std::int64_t value_micro) const
{
    const auto definition_it = relationship_types_.find(type);
    if (definition_it == relationship_types_.end() || definition_it->second.state_thresholds.empty())
        return RelationshipState::Unknown;
    const auto &thresholds = definition_it->second.state_thresholds;
    auto found = std::upper_bound(thresholds.begin(), thresholds.end(), value_micro,
                                  [](std::int64_t value, const RelationshipStateThreshold &threshold) {
                                      return value < threshold.minimum_value_micro;
                                  });
    if (found == thresholds.begin())
        return thresholds.front().state;
    return std::prev(found)->state;
}

foundation::Result<void> SocietyService::ApplySocialChange(SocialChangeRequest request)
{
    if (!definitions_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.definitions_not_frozen", "freeze society definitions before social changes"));
    if (!request.subject.IsValid() || !request.target.IsValid() || !request.type.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.society.invalid_change", "invalid social change"));
    const auto definition_it = relationship_types_.find(request.type);
    if (definition_it == relationship_types_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.society.relationship_type_missing", "relationship type definition missing"));
    const auto &definition = definition_it->second;

    auto *record = FindMutableRelationship(request.subject, request.target, request.type);
    const auto before = record ? record->value_micro : definition.default_value_micro;
    const auto after = std::clamp(SaturatingAdd(before, request.delta_micro), definition.minimum_value_micro,
                                  definition.maximum_value_micro);
    if (!record)
    {
        RelationshipRecord created;
        created.subject = request.subject;
        created.target = request.target;
        created.type = request.type;
        created.value_micro = after;
        created.updated_at = request.context.time;
        auto set = SetRelationship(std::move(created), request.context);
        if (!set)
            return foundation::Result<void>::Failure(std::move(set.GetError()));
        return foundation::Result<void>::Success();
    }

    Bump();
    record->value_micro = after;
    record->state = ResolveRelationshipState(request.type, after);
    record->updated_at = request.context.time;
    record->revision = revision_;
    ++diagnostics_.relationship_changes;
    ++diagnostics_.social_transactions;
    Record({0, SocietyChangeKind::RelationshipChanged, request.subject, request.target, request.type, {}, {},
            request.context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocietyService::SetReputation(ReputationRecord record, GameplayContext context)
{
    if (!definitions_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.definitions_not_frozen", "freeze society definitions before reputation changes"));
    if (!record.subject.IsValid() || !record.scope.IsValid() || !record.track.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.society.invalid_reputation", "invalid reputation"));
    const auto definition_it = reputation_tracks_.find(record.track);
    if (definition_it == reputation_tracks_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.society.reputation_track_missing", "reputation track definition missing"));
    const auto &definition = definition_it->second;
    if (record.value_micro < definition.minimum_value_micro || record.value_micro > definition.maximum_value_micro)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.reputation_value_out_of_range", "reputation value outside definition range"));

    const ReputationKey key{record.subject, record.scope, record.track};
    Bump();
    if (auto it = reputations_.find(key); it != reputations_.end())
    {
        it->second.value_micro = record.value_micro;
        it->second.revision = revision_;
    }
    else
    {
        record.revision = revision_;
        reputations_.emplace(key, record);
        IndexReputation(record);
    }
    ++diagnostics_.reputation_changes;
    Record({0, SocietyChangeKind::ReputationChanged, record.subject, record.scope, {}, record.track, {}, context,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocietyService::ApplyReputationChange(ReputationChangeRequest request)
{
    if (!definitions_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.definitions_not_frozen", "freeze society definitions before reputation changes"));
    if (!request.subject.IsValid() || !request.scope.IsValid() || !request.track.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.society.invalid_reputation", "invalid reputation"));
    const auto definition_it = reputation_tracks_.find(request.track);
    if (definition_it == reputation_tracks_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.society.reputation_track_missing", "reputation track definition missing"));
    const auto &definition = definition_it->second;
    const ReputationKey key{request.subject, request.scope, request.track};
    const auto current_it = reputations_.find(key);
    const auto before = current_it == reputations_.end() ? definition.default_value_micro : current_it->second.value_micro;
    const auto after = std::clamp(SaturatingAdd(before, request.delta_micro), definition.minimum_value_micro,
                                  definition.maximum_value_micro);
    ReputationRecord record{request.subject, request.scope, request.track, after, {}};
    return SetReputation(std::move(record), request.context);
}

const SocialGroupDefinition *SocietyService::GetGroup(GameplayObjectRef group) const noexcept
{
    const auto it = groups_.find(group);
    return it == groups_.end() ? nullptr : &it->second;
}

const RelationshipTypeDefinition *SocietyService::GetRelationshipType(RelationshipTypeId id) const noexcept
{
    const auto it = relationship_types_.find(id);
    return it == relationship_types_.end() ? nullptr : &it->second;
}

const ReputationTrackDefinition *SocietyService::GetReputationTrack(ReputationTrackId id) const noexcept
{
    const auto it = reputation_tracks_.find(id);
    return it == reputation_tracks_.end() ? nullptr : &it->second;
}

const MembershipRecord *SocietyService::GetMembership(MembershipId id) const noexcept
{
    const auto it = memberships_.find(id);
    return it == memberships_.end() ? nullptr : &it->second;
}

std::vector<MembershipRecord> SocietyService::FindGroupsOf(GameplayObjectRef member) const
{
    std::vector<MembershipRecord> out;
    const auto index_it = memberships_by_member_.find(member);
    if (index_it == memberships_by_member_.end())
        return out;
    out.reserve(index_it->second.size());
    for (const auto id : index_it->second)
        if (const auto *record = GetMembership(id))
            out.push_back(*record);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.group != b.group)
            return a.group < b.group;
        return a.id < b.id;
    });
    return out;
}

std::vector<MembershipRecord> SocietyService::FindMembersOf(GameplayObjectRef group) const
{
    std::vector<MembershipRecord> out;
    const auto index_it = memberships_by_group_.find(group);
    if (index_it == memberships_by_group_.end())
        return out;
    out.reserve(index_it->second.size());
    for (const auto id : index_it->second)
        if (const auto *record = GetMembership(id))
            out.push_back(*record);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.member != b.member)
            return a.member < b.member;
        return a.id < b.id;
    });
    return out;
}

std::optional<RelationshipRecord> SocietyService::GetRelationship(GameplayObjectRef subject, GameplayObjectRef target,
                                                                  RelationshipTypeId type) const
{
    const auto key_it = relationship_by_key_.find(RelationshipKey{subject, target, type});
    if (key_it == relationship_by_key_.end())
        return std::nullopt;
    const auto it = relationships_.find(key_it->second);
    if (it == relationships_.end())
        return std::nullopt;
    return it->second;
}

std::int64_t SocietyService::GetEffectiveAttitude(AttitudeQuery query) const
{
    ++diagnostics_.attitude_queries;
    if (!query.observer.IsValid() || !query.target.IsValid())
        return 0;

    std::int64_t result = 0;
    const auto add_relationships = [&](GameplayObjectRef source, std::int64_t RelationshipTypeDefinition::*weight_member) {
        const auto subject_it = relationships_by_subject_.find(source);
        if (subject_it == relationships_by_subject_.end())
            return;
        for (const auto id : subject_it->second)
        {
            const auto record_it = relationships_.find(id);
            if (record_it == relationships_.end() || record_it->second.target != query.target)
                continue;
            const auto definition_it = relationship_types_.find(record_it->second.type);
            if (definition_it == relationship_types_.end())
                continue;
            result = SaturatingAdd(result, ScaleMicro(record_it->second.value_micro, definition_it->second.*weight_member));
        }
    };

    add_relationships(query.observer, &RelationshipTypeDefinition::direct_attitude_weight_micro);

    std::vector<GameplayObjectRef> reputation_scopes;
    reputation_scopes.push_back(query.observer);
    for (const auto &membership : FindGroupsOf(query.observer))
    {
        if (!MembershipContributesToGroupAttitude(membership.state))
            continue;
        add_relationships(membership.group, &RelationshipTypeDefinition::group_attitude_weight_micro);
        reputation_scopes.push_back(membership.group);
    }
    std::sort(reputation_scopes.begin(), reputation_scopes.end());
    reputation_scopes.erase(std::unique(reputation_scopes.begin(), reputation_scopes.end()), reputation_scopes.end());

    for (const auto &[track, definition] : reputation_tracks_)
    {
        if (definition.attitude_weight_micro == 0)
            continue;
        for (const auto scope : reputation_scopes)
        {
            const auto reputation_it = reputations_.find(ReputationKey{query.target, scope, track});
            if (reputation_it != reputations_.end())
                result = SaturatingAdd(result,
                                       ScaleMicro(reputation_it->second.value_micro, definition.attitude_weight_micro));
        }
    }
    return result;
}

std::optional<ReputationRecord> SocietyService::GetReputation(GameplayObjectRef subject, GameplayObjectRef scope,
                                                              ReputationTrackId track) const
{
    const auto it = reputations_.find(ReputationKey{subject, scope, track});
    return it == reputations_.end() ? std::nullopt : std::optional<ReputationRecord>{it->second};
}

SocialStandingSnapshot SocietyService::GetSocialStanding(GameplayObjectRef subject, GameplayObjectRef scope) const
{
    SocialStandingSnapshot out;
    out.subject = subject;
    out.scope = scope;
    out.revision = revision_;

    const auto index_it = reputations_by_subject_.find(subject);
    if (index_it == reputations_by_subject_.end())
        return out;
    for (const auto &key : index_it->second)
    {
        if (key.scope != scope)
            continue;
        const auto record_it = reputations_.find(key);
        if (record_it == reputations_.end())
            continue;
        out.reputations.push_back(record_it->second);
        const auto definition_it = reputation_tracks_.find(record_it->second.track);
        if (definition_it == reputation_tracks_.end())
            continue;
        const auto &thresholds = definition_it->second.standing_thresholds;
        auto found = std::upper_bound(thresholds.begin(), thresholds.end(), record_it->second.value_micro,
                                      [](std::int64_t value, const ReputationStandingThreshold &threshold) {
                                          return value < threshold.minimum_value_micro;
                                      });
        if (found != thresholds.begin())
            out.standing_tags.Add(std::prev(found)->standing_tag);
    }
    std::sort(out.reputations.begin(), out.reputations.end(), [](const auto &a, const auto &b) { return a.track < b.track; });
    return out;
}

bool SocietyService::HasRole(GameplayObjectRef subject, SocialRoleId role, GameplayObjectRef scope) const
{
    const auto index_it = memberships_by_member_.find(subject);
    if (index_it == memberships_by_member_.end())
        return false;
    for (const auto id : index_it->second)
    {
        const auto *membership = GetMembership(id);
        if (membership && membership->role == role && membership->state == MembershipState::Active &&
            (!scope.IsValid() || membership->group == scope))
            return true;
    }
    return false;
}

std::vector<SocietyChange> SocietyService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

SocietyChangeBatch SocietyService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    SocietyChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    batch.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                        : next_change_sequence_ - 1;
    if (!changes_.empty() && changes_.front().sequence > 0 && sequence < changes_.front().sequence - 1)
    {
        batch.snapshot_required = true;
        ++diagnostics_.journal_gaps;
        return batch;
    }
    const auto found = std::upper_bound(changes_.begin(), changes_.end(), sequence,
                                        [](std::uint64_t value, const SocietyChange &change) {
                                            return value < change.sequence;
                                        });
    batch.changes.assign(found, changes_.end());
    return batch;
}

void SocietyService::PruneChangesThrough(std::uint64_t sequence)
{
    while (!changes_.empty() && changes_.front().sequence <= sequence)
        changes_.pop_front();
}

SocietySnapshot SocietyService::CaptureSnapshot() const
{
    SocietySnapshot snapshot;
    snapshot.definitions_frozen = definitions_frozen_;
    for (const auto &[group, definition] : groups_)
    {
        (void)group;
        snapshot.groups.push_back(definition);
    }
    for (const auto &[id, definition] : relationship_types_)
    {
        (void)id;
        snapshot.relationship_types.push_back(definition);
    }
    for (const auto &[id, definition] : reputation_tracks_)
    {
        (void)id;
        snapshot.reputation_tracks.push_back(definition);
    }
    for (const auto &[id, membership] : memberships_)
    {
        (void)id;
        snapshot.memberships.push_back(membership);
    }
    for (const auto &[id, relationship] : relationships_)
    {
        (void)id;
        snapshot.relationships.push_back(relationship);
    }
    for (const auto &[key, reputation] : reputations_)
    {
        (void)key;
        snapshot.reputations.push_back(reputation);
    }

    std::sort(snapshot.groups.begin(), snapshot.groups.end(), [](const auto &a, const auto &b) { return a.group < b.group; });
    std::sort(snapshot.relationship_types.begin(), snapshot.relationship_types.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.reputation_tracks.begin(), snapshot.reputation_tracks.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.memberships.begin(), snapshot.memberships.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.relationships.begin(), snapshot.relationships.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.reputations.begin(), snapshot.reputations.end(), [](const auto &a, const auto &b) {
        if (a.subject != b.subject)
            return a.subject < b.subject;
        if (a.scope != b.scope)
            return a.scope < b.scope;
        return a.track < b.track;
    });
    snapshot.membership_ids = membership_ids_.GetSnapshot();
    snapshot.relationship_ids = relationship_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.journal.assign(changes_.begin(), changes_.end());
    snapshot.next_change_sequence = next_change_sequence_;
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> SocietyService::RestoreSnapshot(SocietySnapshot snapshot)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<GameplayObjectRef, SocialGroupDefinition, RefHash> new_groups;
    std::unordered_map<RelationshipTypeId, RelationshipTypeDefinition, IdHash> new_relationship_types;
    std::unordered_map<ReputationTrackId, ReputationTrackDefinition, IdHash> new_reputation_tracks;
    std::unordered_map<MembershipId, MembershipRecord, IdHash> new_memberships;
    std::unordered_map<MembershipKey, MembershipId, MembershipKeyHash> new_membership_by_key;
    std::unordered_map<GameplayObjectRef, std::vector<MembershipId>, RefHash> new_memberships_by_member;
    std::unordered_map<GameplayObjectRef, std::vector<MembershipId>, RefHash> new_memberships_by_group;
    std::unordered_map<RelationshipId, RelationshipRecord, IdHash> new_relationships;
    std::unordered_map<RelationshipKey, RelationshipId, RelationshipKeyHash> new_relationship_by_key;
    std::unordered_map<GameplayObjectRef, std::vector<RelationshipId>, RefHash> new_relationships_by_subject;
    std::unordered_map<ReputationKey, ReputationRecord, ReputationKeyHash> new_reputations;
    std::unordered_map<GameplayObjectRef, std::vector<ReputationKey>, RefHash> new_reputations_by_subject;

    for (auto definition : snapshot.relationship_types)
    {
        if (!definition.id.IsValid() || definition.minimum_value_micro > definition.maximum_value_micro ||
            definition.default_value_micro < definition.minimum_value_micro ||
            definition.default_value_micro > definition.maximum_value_micro || definition.state_thresholds.empty() ||
            definition.direct_attitude_weight_micro < -kMicroOne || definition.direct_attitude_weight_micro > kMicroOne ||
            definition.group_attitude_weight_micro < -kMicroOne || definition.group_attitude_weight_micro > kMicroOne ||
            definition.revision.value > snapshot.revision.value || new_relationship_types.contains(definition.id))
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_invalid_relationship_definition", "invalid relationship definition snapshot"));
        }
        std::sort(definition.state_thresholds.begin(), definition.state_thresholds.end(), [](const auto &a, const auto &b) {
            return a.minimum_value_micro < b.minimum_value_micro;
        });
        if (definition.state_thresholds.front().minimum_value_micro > definition.minimum_value_micro)
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_invalid_relationship_threshold", "relationship thresholds have a gap"));
        for (std::size_t i = 0; i < definition.state_thresholds.size(); ++i)
        {
            const auto threshold = definition.state_thresholds[i].minimum_value_micro;
            if (threshold < definition.minimum_value_micro || threshold > definition.maximum_value_micro ||
                (i > 0 && threshold == definition.state_thresholds[i - 1].minimum_value_micro))
                return foundation::Result<void>::Failure(
                    Error("gameplay.society.restore_invalid_relationship_threshold", "invalid relationship threshold"));
        }
        new_relationship_types.emplace(definition.id, std::move(definition));
    }

    for (auto definition : snapshot.reputation_tracks)
    {
        if (!definition.id.IsValid() || definition.minimum_value_micro > definition.maximum_value_micro ||
            definition.default_value_micro < definition.minimum_value_micro ||
            definition.default_value_micro > definition.maximum_value_micro ||
            definition.attitude_weight_micro < -kMicroOne || definition.attitude_weight_micro > kMicroOne ||
            definition.revision.value > snapshot.revision.value || new_reputation_tracks.contains(definition.id))
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_invalid_reputation_definition", "invalid reputation definition snapshot"));
        }
        std::sort(definition.standing_thresholds.begin(), definition.standing_thresholds.end(), [](const auto &a, const auto &b) {
            return a.minimum_value_micro < b.minimum_value_micro;
        });
        for (std::size_t i = 0; i < definition.standing_thresholds.size(); ++i)
        {
            const auto &threshold = definition.standing_thresholds[i];
            if (!threshold.standing_tag.IsValid() || threshold.minimum_value_micro < definition.minimum_value_micro ||
                threshold.minimum_value_micro > definition.maximum_value_micro ||
                (i > 0 && threshold.minimum_value_micro == definition.standing_thresholds[i - 1].minimum_value_micro))
                return foundation::Result<void>::Failure(
                    Error("gameplay.society.restore_invalid_reputation_threshold", "invalid reputation threshold"));
        }
        new_reputation_tracks.emplace(definition.id, std::move(definition));
    }

    for (const auto &group : snapshot.groups)
    {
        if (!group.group.IsValid() || !group.type.IsValid() || group.revision.value > snapshot.revision.value ||
            !new_groups.emplace(group.group, group).second)
        {
            return foundation::Result<void>::Failure(Error("gameplay.society.restore_invalid_group", "invalid group snapshot"));
        }
    }

    if ((!snapshot.memberships.empty() || !snapshot.relationships.empty() || !snapshot.reputations.empty()) &&
        !snapshot.definitions_frozen)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.society.restore_definitions_not_frozen", "runtime society snapshot requires frozen definitions"));
    }

    std::uint64_t max_membership_low = 0;
    for (const auto &membership : snapshot.memberships)
    {
        if (!membership.id.IsValid() || !membership.member.IsValid() || !membership.group.IsValid() ||
            !IsValidMembershipState(membership.state) || membership.revision.value > snapshot.revision.value ||
            !new_groups.contains(membership.group))
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_invalid_membership", "invalid membership snapshot"));
        const MembershipKey key{membership.member, membership.group};
        if (new_memberships.contains(membership.id) || new_membership_by_key.contains(key))
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_duplicate_membership", "duplicate membership id or semantic key"));
        new_memberships.emplace(membership.id, membership);
        new_membership_by_key.emplace(key, membership.id);
        new_memberships_by_member[membership.member].push_back(membership.id);
        new_memberships_by_group[membership.group].push_back(membership.id);
        if (membership.id.value.High() == membership_ids_.Scope().Raw())
            max_membership_low = std::max(max_membership_low, membership.id.value.Low());
    }

    std::uint64_t max_relationship_low = 0;
    for (const auto &relationship : snapshot.relationships)
    {
        if (!relationship.id.IsValid() || !relationship.subject.IsValid() || !relationship.target.IsValid() ||
            !relationship.type.IsValid() || relationship.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_invalid_relationship", "invalid relationship snapshot"));
        const auto definition_it = new_relationship_types.find(relationship.type);
        if (definition_it == new_relationship_types.end() || relationship.value_micro < definition_it->second.minimum_value_micro ||
            relationship.value_micro > definition_it->second.maximum_value_micro)
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_relationship_definition_missing", "relationship definition missing or range invalid"));
        const RelationshipKey key{relationship.subject, relationship.target, relationship.type};
        if (new_relationships.contains(relationship.id) || new_relationship_by_key.contains(key))
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_duplicate_relationship", "duplicate relationship id or semantic key"));
        const auto &thresholds = definition_it->second.state_thresholds;
        auto found = std::upper_bound(thresholds.begin(), thresholds.end(), relationship.value_micro,
                                      [](std::int64_t value, const RelationshipStateThreshold &threshold) {
                                          return value < threshold.minimum_value_micro;
                                      });
        const auto derived_state = found == thresholds.begin() ? thresholds.front().state : std::prev(found)->state;
        if (!IsValidRelationshipState(relationship.state) || relationship.state != derived_state)
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_relationship_state_mismatch", "relationship state does not match definition"));
        new_relationships.emplace(relationship.id, relationship);
        new_relationship_by_key.emplace(key, relationship.id);
        new_relationships_by_subject[relationship.subject].push_back(relationship.id);
        if (relationship.id.value.High() == relationship_ids_.Scope().Raw())
            max_relationship_low = std::max(max_relationship_low, relationship.id.value.Low());
    }

    for (const auto &reputation : snapshot.reputations)
    {
        if (!reputation.subject.IsValid() || !reputation.scope.IsValid() || !reputation.track.IsValid() ||
            reputation.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_invalid_reputation", "invalid reputation snapshot"));
        const auto definition_it = new_reputation_tracks.find(reputation.track);
        if (definition_it == new_reputation_tracks.end() || reputation.value_micro < definition_it->second.minimum_value_micro ||
            reputation.value_micro > definition_it->second.maximum_value_micro)
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_reputation_definition_missing", "reputation definition missing or range invalid"));
        const ReputationKey key{reputation.subject, reputation.scope, reputation.track};
        if (!new_reputations.emplace(key, reputation).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_duplicate_reputation", "duplicate reputation semantic key"));
        new_reputations_by_subject[reputation.subject].push_back(key);
    }

    const auto membership_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.membership_ids, membership_ids_.Scope(), max_membership_low);
    const auto relationship_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.relationship_ids, relationship_ids_.Scope(), max_relationship_low);
    if (!membership_generator || !relationship_generator)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.restore_invalid_generator", "invalid society id generator snapshot"));

    if (snapshot.next_change_sequence == 0 || snapshot.journal.size() > kChangeJournalCapacity)
        return foundation::Result<void>::Failure(
            Error("gameplay.society.restore_invalid_journal", "invalid society change journal snapshot"));
    std::deque<SocietyChange> new_journal;
    new_journal.assign(snapshot.journal.begin(), snapshot.journal.end());
    std::uint64_t previous_sequence = 0;
    for (const auto &change : new_journal)
    {
        if (change.sequence == 0 || change.sequence <= previous_sequence || change.sequence >= snapshot.next_change_sequence ||
            change.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.society.restore_invalid_journal", "invalid society change journal sequence"));
        previous_sequence = change.sequence;
    }

    groups_ = std::move(new_groups);
    relationship_types_ = std::move(new_relationship_types);
    reputation_tracks_ = std::move(new_reputation_tracks);
    definitions_frozen_ = snapshot.definitions_frozen;
    memberships_ = std::move(new_memberships);
    membership_by_key_ = std::move(new_membership_by_key);
    memberships_by_member_ = std::move(new_memberships_by_member);
    memberships_by_group_ = std::move(new_memberships_by_group);
    relationships_ = std::move(new_relationships);
    relationship_by_key_ = std::move(new_relationship_by_key);
    relationships_by_subject_ = std::move(new_relationships_by_subject);
    reputations_ = std::move(new_reputations);
    reputations_by_subject_ = std::move(new_reputations_by_subject);
    membership_ids_.Restore(snapshot.membership_ids);
    relationship_ids_.Restore(snapshot.relationship_ids);
    revision_ = snapshot.revision;
    changes_ = std::move(new_journal);
    next_change_sequence_ = snapshot.next_change_sequence;
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

SocietyDiagnostics SocietyService::GetDiagnostics() const noexcept
{
    auto diagnostics = diagnostics_;
    diagnostics.groups = groups_.size();
    diagnostics.memberships = memberships_.size();
    diagnostics.relationships = relationships_.size();
    diagnostics.reputation_records = reputations_.size();
    return diagnostics;
}

void SocietyService::Record(SocietyChange change)
{
    if (next_change_sequence_ == 0)
        return;
    const auto assigned = next_change_sequence_;
    change.sequence = assigned;
    changes_.push_back(std::move(change));
    next_change_sequence_ = assigned == std::numeric_limits<std::uint64_t>::max() ? 0 : assigned + 1;
    while (changes_.size() > kChangeJournalCapacity)
        changes_.pop_front();
}

void SocietyService::IndexMembership(const MembershipRecord &record)
{
    membership_by_key_[MembershipKey{record.member, record.group}] = record.id;
    memberships_by_member_[record.member].push_back(record.id);
    memberships_by_group_[record.group].push_back(record.id);
}

void SocietyService::UnindexMembership(const MembershipRecord &record)
{
    membership_by_key_.erase(MembershipKey{record.member, record.group});
    if (auto it = memberships_by_member_.find(record.member); it != memberships_by_member_.end())
    {
        EraseId(it->second, record.id);
        if (it->second.empty())
            memberships_by_member_.erase(it);
    }
    if (auto it = memberships_by_group_.find(record.group); it != memberships_by_group_.end())
    {
        EraseId(it->second, record.id);
        if (it->second.empty())
            memberships_by_group_.erase(it);
    }
}

void SocietyService::IndexRelationship(const RelationshipRecord &record)
{
    relationship_by_key_[RelationshipKey{record.subject, record.target, record.type}] = record.id;
    relationships_by_subject_[record.subject].push_back(record.id);
}

void SocietyService::IndexReputation(const ReputationRecord &record)
{
    reputations_by_subject_[record.subject].push_back(ReputationKey{record.subject, record.scope, record.track});
}
} // namespace epidemic::gameplay::society
