#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::combat
{
struct DamageTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr DamageTypeId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const DamageTypeId&) const noexcept = default;
};
struct DamageProfileId
{
    TypeId value{};
    [[nodiscard]] static constexpr DamageProfileId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const DamageProfileId&) const noexcept = default;
};
struct CombatResourceTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr CombatResourceTypeId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const CombatResourceTypeId&) const noexcept = default;
};
struct CombatModifierTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr CombatModifierTypeId FromString(std::string_view name) noexcept { return {TypeId::FromString(name)}; }
    [[nodiscard]] constexpr auto operator<=>(const CombatModifierTypeId&) const noexcept = default;
};
struct CombatResolutionId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr auto operator<=>(const CombatResolutionId&) const noexcept = default;
};
struct IdHash
{
    template <typename T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept
    {
        if constexpr (requires { id.value.Raw(); })
            return std::hash<TypeId>{}(id.value);
        else
            return std::hash<GameplayObjectId>{}(id.value);
    }
};

struct CombatResourceDefinition
{
    CombatResourceTypeId id{};
    std::string canonical_name;
    std::int64_t minimum_micro = 0;
    std::int64_t default_maximum_micro = 100'000'000;
    bool drives_life_state = false;
};

struct DamageProfile
{
    DamageProfileId id{};
    std::string canonical_name;
    CombatResourceTypeId target_resource{};
    bool can_miss = true;
    bool can_evade = true;
    bool can_block = true;
    bool can_critical = true;
    std::uint32_t hit_chance_micro = 1'000'000;
    std::uint32_t evade_chance_micro = 0;
    std::uint32_t block_chance_micro = 0;
    std::uint32_t critical_chance_micro = 0;
    std::int64_t blocked_multiplier_micro = 500'000;
    std::int64_t critical_multiplier_micro = 1'500'000;
    std::int64_t flat_mitigation_micro = 0;
};

enum class CombatLifeState { Alive, Downed, Dead, Disabled };
enum class CombatEngagementState { OutOfCombat, Engaged, Disengaging };

struct CombatResourceState
{
    CombatResourceTypeId type{};
    std::int64_t current_micro = 0;
    std::int64_t maximum_micro = 0;
};

struct CombatantRecord
{
    GameplayObjectRef subject{};
    CombatLifeState life_state = CombatLifeState::Alive;
    CombatEngagementState engagement = CombatEngagementState::OutOfCombat;
    std::vector<CombatResourceState> resources;
    Revision revision{};
};

enum class CombatModifierPhase { Base, Attacker, Target, Mitigation, Amplification, Final };
enum class CombatModifierOperation { Add, Multiply, Override, ClampMin, ClampMax };
struct CombatModifier
{
    CombatModifierPhase phase = CombatModifierPhase::Base;
    CombatModifierOperation operation = CombatModifierOperation::Add;
    CombatModifierTypeId type{};
    std::int32_t priority = 0;
    std::int64_t value_micro = 0;
    GameplayObjectRef source{};
};

struct DamageRequest
{
    GameplayObjectRef source{};
    GameplayObjectRef instigator{};
    GameplayObjectRef target{};
    DamageTypeId damage_type{};
    DamageProfileId profile{};
    std::int64_t base_amount_micro = 0;
    random::RandomSeed seed{};
    GameplayContext context{};
};

class ICombatModifierProvider
{
  public:
    virtual ~ICombatModifierProvider() = default;
    [[nodiscard]] virtual std::vector<CombatModifier> Collect(const DamageRequest& request) const = 0;
    [[nodiscard]] virtual Revision RevisionFor(const DamageRequest& request) const noexcept = 0;
};

enum class CombatOutcome : std::uint32_t
{
    None = 0,
    Missed = 1u << 0u,
    Evaded = 1u << 1u,
    Blocked = 1u << 2u,
    Critical = 1u << 3u,
    Killed = 1u << 4u,
};
[[nodiscard]] constexpr CombatOutcome operator|(CombatOutcome a, CombatOutcome b) noexcept { return static_cast<CombatOutcome>(static_cast<std::uint32_t>(a)|static_cast<std::uint32_t>(b)); }
[[nodiscard]] constexpr bool HasOutcome(CombatOutcome value, CombatOutcome flag) noexcept { return (static_cast<std::uint32_t>(value)&static_cast<std::uint32_t>(flag))!=0; }

struct CombatPlan
{
    CombatResolutionId id{};
    DamageRequest request;
    CombatResourceTypeId resource{};
    Revision expected_target_revision{};
    Revision expected_modifier_revision{};
    std::uint64_t expected_provider_epoch = 0;
    std::int64_t requested_amount_micro = 0;
    std::int64_t final_amount_micro = 0;
    CombatOutcome outcomes = CombatOutcome::None;
};

struct CombatResult
{
    CombatResolutionId id{};
    GameplayObjectRef target{};
    CombatResourceTypeId resource{};
    std::int64_t requested_amount_micro = 0;
    std::int64_t final_amount_micro = 0;
    std::int64_t resource_before_micro = 0;
    std::int64_t resource_after_micro = 0;
    CombatLifeState before = CombatLifeState::Alive;
    CombatLifeState after = CombatLifeState::Alive;
    CombatOutcome outcomes = CombatOutcome::None;
    GameplayContext context{};
};

enum class CombatChangeKind { CombatantRegistered, CombatantRemoved, EngagementChanged, DamageResolved, ResourceChanged, LifeStateChanged };
struct CombatChange
{
    std::uint64_t sequence = 0;
    CombatChangeKind kind = CombatChangeKind::CombatantRegistered;
    GameplayObjectRef subject{};
    CombatResolutionId resolution{};
    CombatResourceTypeId resource{};
    CombatLifeState life_state = CombatLifeState::Alive;
    Revision revision{};
    GameplayContext context{};
};

struct CombatChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::vector<CombatChange> changes;
};
struct CombatSnapshot
{
    std::vector<CombatantRecord> combatants;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot resolution_ids{};
    std::vector<CombatChange> journal;
    std::uint64_t next_change_sequence = 1;
};
struct CombatDiagnostics
{
    std::uint64_t combatants = 0;
    std::uint64_t damage_requests = 0;
    std::uint64_t damage_resolved = 0;
    std::uint64_t criticals = 0;
    std::uint64_t blocks = 0;
    std::uint64_t evades = 0;
    std::uint64_t deaths = 0;
    std::uint64_t stale_commits = 0;
};

class CombatService
{
  public:
    CombatService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept { return GameplayDomainId::FromString("framework.combat"); }
    [[nodiscard]] foundation::Result<CombatResourceTypeId> RegisterResource(CombatResourceDefinition definition);
    [[nodiscard]] foundation::Result<DamageTypeId> RegisterDamageType(std::string canonical_name);
    [[nodiscard]] foundation::Result<DamageProfileId> RegisterDamageProfile(DamageProfile profile);
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }
    void SetModifierProvider(const ICombatModifierProvider* provider) noexcept;

    [[nodiscard]] foundation::Result<void> RegisterCombatant(GameplayObjectRef subject, std::vector<CombatResourceState> resources, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveCombatant(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] const CombatantRecord* FindCombatant(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] foundation::Result<CombatResourceState> GetResource(GameplayObjectRef subject, CombatResourceTypeId type) const;
    [[nodiscard]] foundation::Result<void> SetResourceMaximum(GameplayObjectRef subject, CombatResourceTypeId type, std::int64_t maximum_micro, bool preserve_ratio, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ModifyResource(GameplayObjectRef subject, CombatResourceTypeId type, std::int64_t delta_micro, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SetEngagement(GameplayObjectRef subject, CombatEngagementState state, GameplayContext context = {});

    [[nodiscard]] foundation::Result<CombatPlan> PrepareDamage(DamageRequest request);
    [[nodiscard]] foundation::Result<CombatResult> CommitDamage(const CombatPlan& plan);

    [[nodiscard]] std::vector<CombatChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] CombatChangeBatch ReadChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] CombatSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(CombatSnapshot snapshot);
    [[nodiscard]] CombatDiagnostics GetDiagnostics() const noexcept;

  private:
    void Bump(CombatantRecord& record) noexcept;
    void Record(CombatChange change);
    [[nodiscard]] bool ReconcileLifeState(CombatantRecord& record, CombatResourceTypeId changed_resource) noexcept;
    [[nodiscard]] static std::int64_t ApplyModifiers(std::int64_t amount, std::vector<CombatModifier> modifiers) noexcept;

    std::unordered_map<CombatResourceTypeId, CombatResourceDefinition, IdHash> resources_;
    std::unordered_map<DamageTypeId, std::string, IdHash> damage_types_;
    std::unordered_map<DamageProfileId, DamageProfile, IdHash> damage_profiles_;
    std::unordered_map<GameplayObjectRef, CombatantRecord> combatants_;
    const ICombatModifierProvider* modifier_provider_ = nullptr;
    std::uint64_t modifier_provider_epoch_ = 1;
    MonotonicIdGenerator<GameplayObjectId> resolution_ids_;
    std::unordered_map<CombatResolutionId, CombatPlan, IdHash> prepared_plans_;
    bool frozen_ = false;
    static constexpr std::size_t kChangeJournalCapacity = 4096;
    static constexpr std::size_t kPreparedPlanCapacity = 4096;
    std::deque<CombatChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    CombatDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::combat
