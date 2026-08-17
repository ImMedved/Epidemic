#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::gameplay::progression
{
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
struct ProgressionModifierId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const ProgressionModifierId&) const noexcept = default;
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

struct ProgressionTrackDefinition
{
    ProgressionTrackId id{};
    std::string canonical_name;
    std::vector<std::int64_t> rank_thresholds_micro;
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
    Revision revision{};
};

struct ProgressionSnapshot
{
    std::vector<ProgressionProfileSnapshot> profiles;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot modifier_ids{};
    Revision revision{};
};

enum class ProgressionChangeKind
{
    ProfileCreated,
    AttributeChanged,
    ModifierAdded,
    ModifierRemoved,
    TrackProgressChanged,
    TrackRankChanged,
    PerkGranted,
    PerkRevoked,
    UnlockGranted,
    UnlockRevoked,
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
    [[nodiscard]] foundation::Result<void> Freeze();
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] foundation::Result<void> EnsureProfile(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] bool HasProfile(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] foundation::Result<void> SetBaseAttribute(GameplayObjectRef subject, AttributeTypeId attribute, std::int64_t value_micro, GameplayContext context = {});
    [[nodiscard]] foundation::Result<std::int64_t> GetAttribute(GameplayObjectRef subject, AttributeTypeId attribute) const;
    [[nodiscard]] foundation::Result<ProgressionProfileSnapshot> GetProfileSnapshot(GameplayObjectRef subject) const;

    [[nodiscard]] foundation::Result<ProgressionModifierId> AddModifier(GameplayObjectRef subject, ProgressionModifier modifier, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveModifier(GameplayObjectRef subject, ProgressionModifierId modifier, GameplayContext context = {});
    [[nodiscard]] std::uint64_t RemoveModifiersBySource(GameplayObjectRef subject, GameplayObjectRef source, GameplayContext context = {});

    [[nodiscard]] foundation::Result<ProgressionTrackState> GrantProgress(GameplayObjectRef subject, ProgressionTrackId track, std::int64_t amount_micro, GameplayContext context = {});
    [[nodiscard]] foundation::Result<ProgressionTrackState> GetTrack(GameplayObjectRef subject, ProgressionTrackId track) const;

    [[nodiscard]] foundation::Result<void> GrantPerk(GameplayObjectRef subject, PerkDefinitionId perk, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RevokePerk(GameplayObjectRef subject, PerkDefinitionId perk, GameplayContext context = {});
    [[nodiscard]] bool HasPerk(GameplayObjectRef subject, PerkDefinitionId perk) const noexcept;

    [[nodiscard]] foundation::Result<void> GrantUnlock(GameplayObjectRef subject, UnlockRecord unlock, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RevokeUnlock(GameplayObjectRef subject, UnlockTypeId type, TypeId value, GameplayContext context = {});
    [[nodiscard]] bool HasUnlock(GameplayObjectRef subject, UnlockTypeId type, TypeId value) const noexcept;

    [[nodiscard]] std::vector<ProgressionChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] ProgressionSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(ProgressionSnapshot snapshot);
    [[nodiscard]] ProgressionDiagnostics GetDiagnostics() const noexcept;

  private:
    struct Profile
    {
        GameplayObjectRef subject{};
        std::unordered_map<AttributeTypeId, std::int64_t, IdHash> base_attributes;
        std::unordered_map<ProgressionTrackId, ProgressionTrackState, IdHash> tracks;
        std::vector<ProgressionModifier> modifiers;
        std::unordered_set<PerkDefinitionId, IdHash> perks;
        std::vector<UnlockRecord> unlocks;
        Revision revision{};
    };

    [[nodiscard]] foundation::Result<std::int64_t> EvaluateAttribute(const Profile& profile, AttributeTypeId attribute, std::unordered_set<AttributeTypeId, IdHash>& visiting) const;
    [[nodiscard]] Profile* FindProfile(GameplayObjectRef subject) noexcept;
    [[nodiscard]] const Profile* FindProfile(GameplayObjectRef subject) const noexcept;
    void Bump(Profile& profile) noexcept;
    void Record(ProgressionChange change);

    std::unordered_map<AttributeTypeId, AttributeDefinition, IdHash> attributes_;
    std::unordered_map<ProgressionTrackId, ProgressionTrackDefinition, IdHash> tracks_;
    std::unordered_map<PerkDefinitionId, PerkDefinition, IdHash> perks_;
    std::unordered_map<GameplayObjectRef, Profile> profiles_;
    MonotonicIdGenerator<GameplayObjectId> modifier_ids_;
    Revision revision_{};
    bool frozen_ = false;
    std::vector<ProgressionChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    mutable std::uint64_t attribute_reads_ = 0;
    mutable std::uint64_t derived_evaluations_ = 0;
    std::uint64_t track_grants_ = 0;
    std::uint64_t rank_changes_ = 0;
};
} // namespace epidemic::gameplay::progression
