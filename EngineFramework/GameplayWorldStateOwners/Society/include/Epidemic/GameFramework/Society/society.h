#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::society
{
struct MembershipId
{
    GameplayObjectId value{};
    static constexpr MembershipId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; }
    static constexpr MembershipId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const MembershipId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const MembershipId &) const noexcept = default;
};

struct RelationshipId
{
    GameplayObjectId value{};
    static constexpr RelationshipId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr RelationshipId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const RelationshipId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const RelationshipId &) const noexcept = default;
};

struct SocialRoleId
{
    TypeId value{};
    static constexpr SocialRoleId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const SocialRoleId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SocialRoleId &) const noexcept = default;
};

struct SocialGroupTypeId
{
    TypeId value{};
    static constexpr SocialGroupTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const SocialGroupTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SocialGroupTypeId &) const noexcept = default;
};

struct RelationshipTypeId
{
    TypeId value{};
    static constexpr RelationshipTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const RelationshipTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const RelationshipTypeId &) const noexcept = default;
};

struct ReputationTrackId
{
    TypeId value{};
    static constexpr ReputationTrackId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const ReputationTrackId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ReputationTrackId &) const noexcept = default;
};

struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

struct RefHash
{
    [[nodiscard]] std::size_t operator()(GameplayObjectRef r) const noexcept
    {
        return std::hash<GameplayObjectRef>{}(r);
    }
};

struct PairHash
{
    [[nodiscard]] std::size_t operator()(const std::pair<GameplayObjectRef, GameplayObjectRef> &p) const noexcept
    {
        return std::hash<GameplayObjectRef>{}(p.first) ^
               (std::hash<GameplayObjectRef>{}(p.second) + 0x9E3779B97F4A7C15ull);
    }
};

enum class MembershipState
{
    Active,
    Suspended,
    Former,
    Banned,
    Applicant,
    Honorary
};

enum class RelationshipState
{
    Neutral,
    Friendly,
    Hostile,
    Allied,
    Feared,
    Trusted,
    Unknown
};

enum class SocietyChangeKind
{
    GroupCreated,
    MembershipAdded,
    MembershipRemoved,
    MembershipChanged,
    RelationshipChanged,
    ReputationChanged,
    SocialRoleAssigned,
    SocialRoleRemoved,
    FactionRelationChanged
};

struct RelationshipStateThreshold
{
    std::int64_t minimum_value_micro = 0;
    RelationshipState state = RelationshipState::Neutral;
};

struct RelationshipTypeDefinition
{
    RelationshipTypeId id{};
    std::int64_t minimum_value_micro = -1'000'000;
    std::int64_t maximum_value_micro = 1'000'000;
    std::int64_t default_value_micro = 0;
    // Sorted ascending at registration. The state of a value is the last threshold whose
    // minimum_value_micro is <= value. The first threshold must cover minimum_value_micro.
    std::vector<RelationshipStateThreshold> state_thresholds;
    // Normalized coefficients in [-1'000'000, 1'000'000].
    std::int64_t direct_attitude_weight_micro = 1'000'000;
    std::int64_t group_attitude_weight_micro = 0;
    Revision revision{};
};

struct ReputationStandingThreshold
{
    std::int64_t minimum_value_micro = 0;
    TagId standing_tag{};
};

struct ReputationTrackDefinition
{
    ReputationTrackId id{};
    std::int64_t minimum_value_micro = -1'000'000;
    std::int64_t maximum_value_micro = 1'000'000;
    std::int64_t default_value_micro = 0;
    // Contribution of this track to GetEffectiveAttitude when the target has this reputation
    // in the observer's own scope or in a group the observer actively belongs to.
    std::int64_t attitude_weight_micro = 0;
    // Sorted ascending. At most one, the highest applicable threshold, contributes a standing tag.
    std::vector<ReputationStandingThreshold> standing_thresholds;
    Revision revision{};
};

struct SocialGroupDefinition
{
    GameplayObjectRef group{};
    GameplayTagSet tags;
    SocialGroupTypeId type{};
    std::vector<std::byte> payload;
    Revision revision{};
};

struct MembershipRecord
{
    MembershipId id{};
    GameplayObjectRef member{};
    GameplayObjectRef group{};
    SocialRoleId role{};
    std::int64_t rank = 0;
    MembershipState state = MembershipState::Active;
    GameplayTimePoint joined_at{};
    Revision revision{};
};

struct RelationshipRecord
{
    RelationshipId id{};
    GameplayObjectRef subject{};
    GameplayObjectRef target{};
    RelationshipTypeId type{};
    std::int64_t value_micro = 0;
    // Derived from RelationshipTypeDefinition::state_thresholds on every mutation/restore.
    RelationshipState state = RelationshipState::Neutral;
    GameplayTimePoint updated_at{};
    Revision revision{};
};

struct ReputationRecord
{
    GameplayObjectRef subject{};
    GameplayObjectRef scope{};
    ReputationTrackId track{};
    std::int64_t value_micro = 0;
    Revision revision{};
};

struct SocialStandingSnapshot
{
    GameplayObjectRef subject{};
    GameplayObjectRef scope{};
    GameplayTagSet standing_tags;
    std::vector<ReputationRecord> reputations;
    Revision revision{};
};

struct SocialChangeRequest
{
    GameplayObjectRef subject{};
    GameplayObjectRef target{};
    RelationshipTypeId type{};
    std::int64_t delta_micro = 0;
    TypeId reason{};
    GameplayContext context{};
};

struct ReputationChangeRequest
{
    GameplayObjectRef subject{};
    GameplayObjectRef scope{};
    ReputationTrackId track{};
    std::int64_t delta_micro = 0;
    TypeId reason{};
    GameplayContext context{};
};

struct AttitudeQuery
{
    GameplayObjectRef observer{};
    GameplayObjectRef target{};
    GameplayContext context{};
};

struct SocietyChange
{
    std::uint64_t sequence = 0;
    SocietyChangeKind kind = SocietyChangeKind::GroupCreated;
    GameplayObjectRef subject{};
    GameplayObjectRef target{};
    RelationshipTypeId relationship_type{};
    ReputationTrackId reputation_track{};
    MembershipId membership{};
    GameplayContext context{};
    Revision revision{};
};

struct SocietyChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;
    std::vector<SocietyChange> changes;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};

struct SocietySnapshot
{
    std::vector<SocialGroupDefinition> groups;
    std::vector<RelationshipTypeDefinition> relationship_types;
    std::vector<ReputationTrackDefinition> reputation_tracks;
    bool definitions_frozen = false;
    std::vector<MembershipRecord> memberships;
    std::vector<RelationshipRecord> relationships;
    std::vector<ReputationRecord> reputations;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot membership_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot relationship_ids{};
    Revision revision{};
    std::vector<SocietyChange> journal;
    std::uint64_t next_change_sequence = 1;

    std::uint64_t change_epoch = 1;
};

struct SocietyDiagnostics
{
    std::uint64_t groups = 0;
    std::uint64_t memberships = 0;
    std::uint64_t relationships = 0;
    std::uint64_t reputation_records = 0;
    std::uint64_t relationship_changes = 0;
    std::uint64_t reputation_changes = 0;
    std::uint64_t attitude_queries = 0;
    std::uint64_t social_transactions = 0;
    std::uint64_t journal_gaps = 0;
};

class SocietyService
{
  public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.society");
    }

    [[nodiscard]] foundation::Result<void> RegisterGroup(SocialGroupDefinition group);
    [[nodiscard]] foundation::Result<void> RegisterRelationshipType(RelationshipTypeDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterReputationTrack(ReputationTrackDefinition definition);
    [[nodiscard]] foundation::Result<void> FreezeDefinitions();
    [[nodiscard]] bool DefinitionsFrozen() const noexcept { return definitions_frozen_; }

    [[nodiscard]] foundation::Result<MembershipId> AddMembership(MembershipRecord record);
    [[nodiscard]] foundation::Result<void> SetMembershipState(MembershipId id, MembershipState state,
                                                              GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SetMembershipRole(MembershipId id, SocialRoleId role,
                                                             GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveMembership(MembershipId id, GameplayContext context = {});

    [[nodiscard]] foundation::Result<RelationshipId> SetRelationship(RelationshipRecord record,
                                                                     GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ApplySocialChange(SocialChangeRequest request);
    [[nodiscard]] foundation::Result<void> SetReputation(ReputationRecord record, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ApplyReputationChange(ReputationChangeRequest request);

    [[nodiscard]] const SocialGroupDefinition *GetGroup(GameplayObjectRef group) const noexcept;
    [[nodiscard]] const RelationshipTypeDefinition *GetRelationshipType(RelationshipTypeId id) const noexcept;
    [[nodiscard]] const ReputationTrackDefinition *GetReputationTrack(ReputationTrackId id) const noexcept;
    [[nodiscard]] const MembershipRecord *GetMembership(MembershipId id) const noexcept;
    [[nodiscard]] std::vector<MembershipRecord> FindGroupsOf(GameplayObjectRef member) const;
    [[nodiscard]] std::vector<MembershipRecord> FindMembersOf(GameplayObjectRef group) const;
    [[nodiscard]] std::optional<RelationshipRecord> GetRelationship(GameplayObjectRef subject, GameplayObjectRef target,
                                                                    RelationshipTypeId type) const;
    [[nodiscard]] std::int64_t GetEffectiveAttitude(AttitudeQuery query) const;
    [[nodiscard]] std::optional<ReputationRecord> GetReputation(GameplayObjectRef subject, GameplayObjectRef scope,
                                                                ReputationTrackId track) const;
    [[nodiscard]] SocialStandingSnapshot GetSocialStanding(GameplayObjectRef subject, GameplayObjectRef scope) const;
    [[nodiscard]] bool HasRole(GameplayObjectRef subject, SocialRoleId role, GameplayObjectRef scope = {}) const;

    private:
        [[nodiscard]] std::vector<SocietyChange> ChangesSinceSequence(std::uint64_t sequence) const;
        [[nodiscard]] SocietyChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] SocietyChangeBatch ReadChangesSince(ChangeCursor cursor) const
    {
        auto batch = ReadChangesSinceSequence(cursor.sequence);
        batch.oldest_available_cursor = {journal_epoch_, batch.oldest_available_sequence};
        batch.latest_cursor = {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                                    : next_change_sequence_ - 1};
        if ((!cursor.IsValid() && cursor.sequence != 0) || (cursor.IsValid() && cursor.epoch != journal_epoch_))
        {
            batch.changes.clear();
            batch.snapshot_required = true;
        }
        return batch;
    }
    [[nodiscard]] ChangeCursor LatestChangeCursor() const noexcept
    {
        return {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                          : next_change_sequence_ - 1};
    }
    void PruneChangesThrough(std::uint64_t sequence);

    [[nodiscard]] SocietySnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(SocietySnapshot snapshot);
    [[nodiscard]] SocietyDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    struct MembershipKey
    {
        GameplayObjectRef member{};
        GameplayObjectRef group{};
        [[nodiscard]] bool operator==(const MembershipKey &) const noexcept = default;
    };
    struct MembershipKeyHash
    {
        [[nodiscard]] std::size_t operator()(const MembershipKey &key) const noexcept;
    };
    struct RelationshipKey
    {
        GameplayObjectRef subject{};
        GameplayObjectRef target{};
        RelationshipTypeId type{};
        [[nodiscard]] bool operator==(const RelationshipKey &) const noexcept = default;
    };
    struct RelationshipKeyHash
    {
        [[nodiscard]] std::size_t operator()(const RelationshipKey &key) const noexcept;
    };
    struct ReputationKey
    {
        GameplayObjectRef subject{};
        GameplayObjectRef scope{};
        ReputationTrackId track{};
        [[nodiscard]] bool operator==(const ReputationKey &) const noexcept = default;
    };
    struct ReputationKeyHash
    {
        [[nodiscard]] std::size_t operator()(const ReputationKey &key) const noexcept;
    };

    static constexpr std::size_t kChangeJournalCapacity = 4096;

    void Bump() noexcept { revision_.value++; }
    void Record(SocietyChange change);
    [[nodiscard]] RelationshipRecord *FindMutableRelationship(GameplayObjectRef subject, GameplayObjectRef target,
                                                              RelationshipTypeId type) noexcept;
    [[nodiscard]] RelationshipState ResolveRelationshipState(RelationshipTypeId type, std::int64_t value_micro) const;
    void IndexMembership(const MembershipRecord &record);
    void UnindexMembership(const MembershipRecord &record);
    void IndexRelationship(const RelationshipRecord &record);
    void IndexReputation(const ReputationRecord &record);

    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> membership_ids_{0x2400};
    MonotonicIdGenerator<GameplayObjectId> relationship_ids_{0x2401};

    std::unordered_map<GameplayObjectRef, SocialGroupDefinition, RefHash> groups_;
    std::unordered_map<RelationshipTypeId, RelationshipTypeDefinition, IdHash> relationship_types_;
    std::unordered_map<ReputationTrackId, ReputationTrackDefinition, IdHash> reputation_tracks_;
    bool definitions_frozen_ = false;

    std::unordered_map<MembershipId, MembershipRecord, IdHash> memberships_;
    std::unordered_map<MembershipKey, MembershipId, MembershipKeyHash> membership_by_key_;
    std::unordered_map<GameplayObjectRef, std::vector<MembershipId>, RefHash> memberships_by_member_;
    std::unordered_map<GameplayObjectRef, std::vector<MembershipId>, RefHash> memberships_by_group_;

    std::unordered_map<RelationshipId, RelationshipRecord, IdHash> relationships_;
    std::unordered_map<RelationshipKey, RelationshipId, RelationshipKeyHash> relationship_by_key_;
    std::unordered_map<GameplayObjectRef, std::vector<RelationshipId>, RefHash> relationships_by_subject_;

    std::unordered_map<ReputationKey, ReputationRecord, ReputationKeyHash> reputations_;
    std::unordered_map<GameplayObjectRef, std::vector<ReputationKey>, RefHash> reputations_by_subject_;

    std::deque<SocietyChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    mutable SocietyDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::society

namespace std
{
template <> struct hash<epidemic::gameplay::society::MembershipId>
{
    size_t operator()(const epidemic::gameplay::society::MembershipId &v) const noexcept
    {
        return hash<epidemic::gameplay::GameplayObjectId>{}(v.value);
    }
};
template <> struct hash<epidemic::gameplay::society::RelationshipId>
{
    size_t operator()(const epidemic::gameplay::society::RelationshipId &v) const noexcept
    {
        return hash<epidemic::gameplay::GameplayObjectId>{}(v.value);
    }
};
} // namespace std
