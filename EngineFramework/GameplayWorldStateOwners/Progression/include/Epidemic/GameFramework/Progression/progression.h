#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::gameplay::progression
{
struct ProgressionChange;
struct AttributeTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr AttributeTypeId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const AttributeTypeId&) const noexcept = default;
};
struct ProgressionTrackId
{
    TypeId value{};
    [[nodiscard]] static constexpr ProgressionTrackId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const ProgressionTrackId&) const noexcept = default;
};
struct PerkDefinitionId
{
    TypeId value{};
    [[nodiscard]] static constexpr PerkDefinitionId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const PerkDefinitionId&) const noexcept = default;
};
struct UnlockTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr UnlockTypeId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const UnlockTypeId&) const noexcept = default;
};
struct MilestoneId
{
    TypeId value{};
    [[nodiscard]] static constexpr MilestoneId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const MilestoneId&) const noexcept = default;
};
struct UnlockDefinitionId
{
    TypeId value{};
    [[nodiscard]] static constexpr UnlockDefinitionId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const UnlockDefinitionId&) const noexcept = default;
};
struct ProgressionModifierId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const ProgressionModifierId&) const noexcept = default;
};
struct ProgressionGrantReservationId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const ProgressionGrantReservationId&) const noexcept = default;
};

struct IdHash
{
    template <typename T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept
    {
        if constexpr (requires { id.value.Raw(); }) return std::hash<TypeId>{}(id.value);
        else return std::hash<GameplayObjectId>{}(id.value);
    }
};

struct DerivedTerm
{
    AttributeTypeId source{};
    std::int64_t coefficient_micro = 1'000'000;
};

struct AttributeDefinition
{
    AttributeTypeId id{};
    std::string canonical_name;
    std::int64_t default_micro = 0;
    std::int64_t min_micro = INT64_MIN;
    std::int64_t max_micro = INT64_MAX;
    std::int64_t derived_bias_micro = 0;
    std::vector<DerivedTerm> derived_terms;
};

struct ProgressionTrackPolicy
{
    std::int64_t min_progress_micro = 0;
    std::int64_t max_progress_micro = std::numeric_limits<std::int64_t>::max();
    bool allow_decrease = false;
    bool allow_direct_set = false;
};

struct ProgressionTrackDefinition
{
    ProgressionTrackId id{};
    std::string canonical_name;
    std::vector<std::int64_t> rank_thresholds_micro;
    ProgressionTrackPolicy policy{};
};

enum class ProgressionPrerequisiteKind
{
    TrackProgressAtLeast,
    TrackRankAtLeast,
    HasPerk,
    HasUnlock
};
struct ProgressionPrerequisite
{
    ProgressionPrerequisiteKind kind = ProgressionPrerequisiteKind::TrackProgressAtLeast;
    ProgressionTrackId track{};
    std::int64_t progress_micro = 0;
    std::uint32_t rank = 0;
    PerkDefinitionId perk{};
    UnlockTypeId unlock_type{};
    TypeId unlock_value{};
};
struct UnlockDefinition
{
    UnlockDefinitionId id{};
    std::string canonical_name;
    UnlockTypeId type{};
    TypeId value{};
};
struct MilestoneDefinition
{
    MilestoneId id{};
    std::string canonical_name;
    ProgressionTrackId track{};
    std::int64_t threshold_micro = 0;
    std::vector<ProgressionPrerequisite> prerequisites;
    std::vector<UnlockDefinitionId> unlocks;
};
struct PrerequisiteEvaluation
{
    bool satisfied = true;
    std::vector<std::size_t> unmet_indices;
};

struct PerkDefinition
{
    PerkDefinitionId id{};
    std::string canonical_name;
    GameplayTagSet tags;
};

enum class ModifierOperation
{
    BaseAdd,
    BaseMultiply,
    FinalAdd,
    FinalMultiply,
    Override,
    ClampMin,
    ClampMax,
};

struct ProgressionModifier
{
    ProgressionModifierId id{};
    AttributeTypeId target{};
    ModifierOperation operation = ModifierOperation::BaseAdd;
    std::int64_t value_micro = 0;
    std::int32_t priority = 0;
    TypeId modifier_type{};
    GameplayObjectRef source{};
    bool persistent = true;
};

struct ProgressionTrackState
{
    ProgressionTrackId id{};
    std::int64_t progress_micro = 0;
    std::uint32_t rank = 0;
    Revision revision{};
};

struct UnlockRecord
{
    UnlockTypeId type{};
    TypeId value{};
    GameplayObjectRef source{};
    [[nodiscard]] bool operator==(const UnlockRecord&) const noexcept = default;
};

struct ProgressionProfileSnapshot
{
    GameplayObjectRef subject{};
    std::vector<std::pair<AttributeTypeId, std::int64_t>> base_attributes;
    std::vector<ProgressionTrackState> tracks;
    std::vector<ProgressionModifier> modifiers;
    std::vector<PerkDefinitionId> perks;
    std::vector<UnlockRecord> unlocks;
    std::vector<MilestoneId> achieved_milestones;
    Revision revision{};
};

enum class ProgressionChangeKind
{
    ProfileCreated,
    ProfileRemoved,
    AttributeChanged,
    ModifierAdded,
    ModifierRemoved,
    TrackProgressChanged,
    TrackRankChanged,
    PerkGranted,
    PerkRevoked,
    UnlockGranted,
    UnlockRevoked,
    MilestoneReached,
};

struct ProgressionChange
{
    std::uint64_t sequence = 0;
    ProgressionChangeKind kind = ProgressionChangeKind::ProfileCreated;
    GameplayObjectRef subject{};
    AttributeTypeId attribute{};
    ProgressionTrackId track{};
    PerkDefinitionId perk{};
    ProgressionModifierId modifier{};
    Revision revision{};
    GameplayContext context{};
    MilestoneId milestone{};
    UnlockTypeId unlock_type{};
    TypeId unlock_value{};
};

struct ProgressionChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::vector<ProgressionChange> changes;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};

struct ProgressionSnapshot
{
    std::vector<ProgressionProfileSnapshot> profiles;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot modifier_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot grant_reservation_ids{};
    Revision revision{};
    std::vector<ProgressionChange> journal;
    std::uint64_t next_change_sequence = 1;

    std::uint64_t change_epoch = 1;
};

struct ProgressionDiagnostics
{
    std::uint64_t profiles = 0;
    std::uint64_t attribute_reads = 0;
    std::uint64_t derived_evaluations = 0;
    std::uint64_t active_modifiers = 0;
    std::uint64_t track_grants = 0;
    std::uint64_t rank_changes = 0;
};

class ProgressionService
{
  public:
    ProgressionService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept { return GameplayDomainId::FromString("framework.progression"); }

    [[nodiscard]] foundation::Result<AttributeTypeId> RegisterAttribute(AttributeDefinition definition);
    [[nodiscard]] foundation::Result<ProgressionTrackId> RegisterTrack(ProgressionTrackDefinition definition);
    [[nodiscard]] foundation::Result<PerkDefinitionId> RegisterPerk(PerkDefinition definition);
    [[nodiscard]] foundation::Result<UnlockDefinitionId> RegisterUnlockDefinition(UnlockDefinition definition);
    [[nodiscard]] foundation::Result<MilestoneId> RegisterMilestone(MilestoneDefinition definition);
    [[nodiscard]] foundation::Result<void> Freeze();
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] foundation::Result<void> EnsureProfile(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveProfile(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] bool HasProfile(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] foundation::Result<void> SetBaseAttribute(GameplayObjectRef subject, AttributeTypeId attribute, std::int64_t value_micro, GameplayContext context = {});
    [[nodiscard]] foundation::Result<std::int64_t> GetAttribute(GameplayObjectRef subject, AttributeTypeId attribute) const;
    [[nodiscard]] foundation::Result<ProgressionProfileSnapshot> GetProfileSnapshot(GameplayObjectRef subject) const;

    [[nodiscard]] foundation::Result<ProgressionModifierId> AddModifier(GameplayObjectRef subject, ProgressionModifier modifier, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveModifier(GameplayObjectRef subject, ProgressionModifierId modifier, GameplayContext context = {});
    [[nodiscard]] std::uint64_t RemoveModifiersBySource(GameplayObjectRef subject, GameplayObjectRef source, GameplayContext context = {});
    // Atomically replaces the complete derived modifier projection for one semantic source.
    // All validation and ID allocation happen before the profile is mutated.
    [[nodiscard]] foundation::Result<std::vector<ProgressionModifierId>> ReplaceModifiersBySource(
        GameplayObjectRef subject, GameplayObjectRef source, std::vector<ProgressionModifier> modifiers,
        GameplayContext context = {});

    [[nodiscard]] foundation::Result<ProgressionTrackState> GrantProgress(GameplayObjectRef subject, ProgressionTrackId track, std::int64_t amount_micro, GameplayContext context = {});
    [[nodiscard]] foundation::Result<ProgressionTrackState> SetProgress(GameplayObjectRef subject, ProgressionTrackId track, std::int64_t progress_micro, GameplayContext context = {});
    [[nodiscard]] foundation::Result<ProgressionTrackState> GetTrack(GameplayObjectRef subject, ProgressionTrackId track) const;
    // Transaction primitive for cross-major reward delivery. Reserve is the only fallible stage.
    // Reserved progress is invisible to normal queries and snapshots until Commit. A subject can have
    // only one active progress reservation, preventing conflicting track/perk/unlock mutations.
    // Commit and Release are noexcept/idempotent.
    [[nodiscard]] foundation::Result<ProgressionGrantReservationId> ReserveProgressGrant(
        GameplayObjectRef subject, ProgressionTrackId track, std::int64_t amount_micro, GameplayContext context = {});
    void CommitProgressGrant(ProgressionGrantReservationId reservation) noexcept;
    void ReleaseProgressGrant(ProgressionGrantReservationId reservation) noexcept;

    [[nodiscard]] foundation::Result<void> GrantPerk(GameplayObjectRef subject, PerkDefinitionId perk, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RevokePerk(GameplayObjectRef subject, PerkDefinitionId perk, GameplayContext context = {});
    [[nodiscard]] bool HasPerk(GameplayObjectRef subject, PerkDefinitionId perk) const noexcept;

    [[nodiscard]] foundation::Result<void> GrantUnlock(GameplayObjectRef subject, UnlockRecord unlock, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RevokeUnlock(GameplayObjectRef subject, UnlockTypeId type, TypeId value, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RevokeUnlockFromSource(GameplayObjectRef subject, UnlockTypeId type, TypeId value, GameplayObjectRef source, GameplayContext context = {});
    [[nodiscard]] bool HasUnlock(GameplayObjectRef subject, UnlockTypeId type, TypeId value) const noexcept;
    [[nodiscard]] PrerequisiteEvaluation QueryPrerequisites(GameplayObjectRef subject, const std::vector<ProgressionPrerequisite>& prerequisites) const;
    [[nodiscard]] foundation::Result<std::vector<MilestoneId>> EvaluateMilestones(GameplayObjectRef subject, GameplayContext context = {});

    private:
        [[nodiscard]] std::vector<ProgressionChange> ChangesSinceSequence(std::uint64_t sequence) const;
        [[nodiscard]] ProgressionChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] ProgressionChangeBatch ReadChangesSince(ChangeCursor cursor) const
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
    [[nodiscard]] ProgressionSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(ProgressionSnapshot snapshot);
    [[nodiscard]] ProgressionDiagnostics GetDiagnostics() const noexcept;

  private:
    struct PendingProgressGrant
    {
        ProgressionGrantReservationId id{};
        GameplayObjectRef subject{};
        ProgressionTrackId track{};
        bool track_existed = false;
        ProgressionTrackState before{};
        ProgressionTrackState after{};
        GameplayContext context{};
    };
    struct Profile
    {
        GameplayObjectRef subject{};
        std::unordered_map<AttributeTypeId, std::int64_t, IdHash> base_attributes;
        std::unordered_map<ProgressionTrackId, ProgressionTrackState, IdHash> tracks;
        std::vector<ProgressionModifier> modifiers;
        std::unordered_set<PerkDefinitionId, IdHash> perks;
        std::vector<UnlockRecord> unlocks;
        std::unordered_set<MilestoneId, IdHash> achieved_milestones;
        Revision revision{};
    };

    [[nodiscard]] foundation::Result<std::int64_t> EvaluateAttribute(const Profile& profile, AttributeTypeId attribute, std::unordered_set<AttributeTypeId, IdHash>& visiting) const;
    [[nodiscard]] Profile* FindProfile(GameplayObjectRef subject) noexcept;
    [[nodiscard]] const Profile* FindProfile(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] bool HasPendingProgressGrant(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] foundation::Result<std::vector<MilestoneId>> EvaluateMilestonesUnlocked(
        Profile& profile, GameplayObjectRef subject, GameplayContext context);
    bool Bump(Profile& profile) noexcept;
    [[nodiscard]] bool CanBump(const Profile& profile) const noexcept;
    void Record(ProgressionChange change);

    std::unordered_map<AttributeTypeId, AttributeDefinition, IdHash> attributes_;
    std::unordered_map<ProgressionTrackId, ProgressionTrackDefinition, IdHash> tracks_;
    std::unordered_map<PerkDefinitionId, PerkDefinition, IdHash> perks_;
    std::unordered_map<UnlockDefinitionId, UnlockDefinition, IdHash> unlock_definitions_;
    std::unordered_map<MilestoneId, MilestoneDefinition, IdHash> milestones_;
    std::unordered_map<GameplayObjectRef, Profile> profiles_;
    MonotonicIdGenerator<GameplayObjectId> modifier_ids_;
    MonotonicIdGenerator<GameplayObjectId> grant_reservation_ids_;
    std::unordered_map<ProgressionGrantReservationId, PendingProgressGrant, IdHash> pending_progress_grants_;
    Revision revision_{};
    bool frozen_ = false;
    static constexpr std::size_t kChangeJournalCapacity = 4096;
    std::deque<ProgressionChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    mutable std::uint64_t attribute_reads_ = 0;
    mutable std::uint64_t derived_evaluations_ = 0;
    std::uint64_t track_grants_ = 0;
    std::uint64_t rank_changes_ = 0;
};
} // namespace epidemic::gameplay::progression
