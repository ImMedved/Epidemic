#pragma once

#include "Epidemic/GameFramework/Abilities/abilities.h"
#include "Epidemic/GameFramework/Combat/combat.h"
#include "Epidemic/GameFramework/Conditions/conditions.h"
#include "Epidemic/GameFramework/Effects/effects.h"
#include "Epidemic/GameFramework/Loot/loot.h"
#include "Epidemic/GameFramework/Progression/progression.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::integration
{
struct CombatDamageEffectPayload
{
    combat::DamageTypeId damage_type{};
    combat::DamageProfileId profile{};
    std::uint64_t seed_salt=0;
};

class CombatDamageEffectHandler final : public effects::IEffectHandler
{
  public:
    explicit CombatDamageEffectHandler(combat::CombatService& combat) : combat_(combat) {}
    [[nodiscard]] static constexpr effects::EffectTypeId StaticType() noexcept { return effects::EffectTypeId::FromString("framework.combat.damage"); }
    [[nodiscard]] static constexpr TypeId PayloadType() noexcept { return TypeId::FromString("framework.combat.damage.payload"); }
    [[nodiscard]] static constexpr TypeId PlanTokenType() noexcept { return TypeId::FromString("framework.combat.damage.plan"); }
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override { return StaticType(); }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override;
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(const effects::EffectOperation& operation,const effects::RegisteredEffectPayload& token) noexcept override;
  private:
    combat::CombatService& combat_;
};

enum class ProgressionCombatRole { Attacker, Target };
struct ProgressionCombatMapping
{
    progression::AttributeTypeId attribute{};
    ProgressionCombatRole role=ProgressionCombatRole::Attacker;
    combat::CombatModifierPhase phase=combat::CombatModifierPhase::Attacker;
    combat::CombatModifierOperation operation=combat::CombatModifierOperation::Add;
    combat::CombatModifierTypeId type{};
    std::int32_t priority=0;
    std::int64_t scale_micro=1'000'000;
};
class ProgressionCombatModifierProvider final : public combat::ICombatModifierProvider
{
  public:
    explicit ProgressionCombatModifierProvider(const progression::ProgressionService& progression) : progression_(progression) {}
    [[nodiscard]] foundation::Result<void> AddMapping(ProgressionCombatMapping mapping);
    [[nodiscard]] foundation::Result<void> FreezeMappings();
    [[nodiscard]] bool MappingsFrozen() const noexcept { return mappings_frozen_; }
    [[nodiscard]] std::uint64_t MappingRevision() const noexcept { return mapping_revision_; }
    [[nodiscard]] std::vector<combat::CombatModifier> Collect(const combat::DamageRequest& request) const override;
    [[nodiscard]] Revision RevisionFor(const combat::DamageRequest& request) const noexcept override;
  private:
    const progression::ProgressionService& progression_;
    std::vector<ProgressionCombatMapping> mappings_;
    bool mappings_frozen_ = false;
    std::uint64_t mapping_revision_ = 0;
};

struct CombatAbilityResourceMapping
{
    abilities::AbilityResourceTypeId ability{};
    combat::CombatResourceTypeId combat{};
};
class CombatAbilityResourceProvider final : public abilities::IAbilityResourceProvider
{
  public:
    explicit CombatAbilityResourceProvider(combat::CombatService& combat) : combat_(combat) {}
    [[nodiscard]] foundation::Result<void> AddMapping(CombatAbilityResourceMapping mapping);
    [[nodiscard]] foundation::Result<void> FreezeMappings();
    [[nodiscard]] bool MappingsFrozen() const noexcept { return mappings_frozen_; }
    [[nodiscard]] std::uint64_t MappingRevision() const noexcept { return mapping_revision_; }
    [[nodiscard]] bool CanAfford(GameplayObjectRef owner,abilities::AbilityResourceTypeId type,std::int64_t amount_micro) const override;
    [[nodiscard]] foundation::Result<abilities::AbilityResourceReservation> Reserve(GameplayObjectRef owner,abilities::AbilityResourceTypeId type,std::int64_t amount_micro,GameplayContext context) override;
    void Commit(const abilities::AbilityResourceReservation& reservation,GameplayContext context) noexcept override;
    void Release(const abilities::AbilityResourceReservation& reservation,GameplayContext context) noexcept override;
    [[nodiscard]] foundation::Result<void> ReconcileReservation(const abilities::AbilityResourceReservation& reservation,
                                                                GameplayObjectRef owner,
                                                                GameplayContext context) override;
  private:
    struct ReservationToken
    {
        combat::CombatResourceReservationId reservation{};
        combat::CombatResourceTypeId resource{};
        std::int64_t amount_micro = 0;
    };
    static constexpr TypeId TokenType() noexcept { return TypeId::FromString("framework.abilities.combat_resource.reservation.v2"); }
    combat::CombatService& combat_;
    std::unordered_map<abilities::AbilityResourceTypeId,combat::CombatResourceTypeId,abilities::IdHash> mappings_;
    bool mappings_frozen_ = false;
    std::uint64_t mapping_revision_ = 0;
};

struct AbilityEffectDeliveryKey
{
    abilities::AbilityExecutionId execution{};
    GameplayTimePoint occurrence_at{};
    std::uint32_t output_index = 0;
    [[nodiscard]] constexpr auto operator<=>(const AbilityEffectDeliveryKey&) const noexcept = default;
};
struct AbilityEffectDeliveryRecord
{
    AbilityEffectDeliveryKey key{};
    effects::EffectExecutionResult result;
};
struct AbilityEffectsCheckpoint
{
    std::uint64_t mapping_revision = 0;
    std::vector<AbilityEffectDeliveryRecord> delivered;
};
class AbilityEffectsDispatcher
{
  public:
    explicit AbilityEffectsDispatcher(effects::EffectService& effects) : effects_(effects) {}
    [[nodiscard]] foundation::Result<void> Map(ActionTypeId action,effects::EffectDefinitionId definition);
    [[nodiscard]] foundation::Result<void> FreezeMappings();
    [[nodiscard]] std::uint64_t MappingRevision() const noexcept { return mapping_revision_; }
    [[nodiscard]] foundation::Result<std::vector<effects::EffectExecutionResult>> Dispatch(std::span<const abilities::AbilityOutput> outputs);
    [[nodiscard]] AbilityEffectsCheckpoint CaptureCheckpoint() const;
    [[nodiscard]] foundation::Result<void> RestoreCheckpoint(AbilityEffectsCheckpoint checkpoint);
  private:
    friend class AbilityOutputDeliveryCoordinator;
    std::uint64_t PruneDeliveriesForExecution(abilities::AbilityExecutionId execution) noexcept;
    effects::EffectService& effects_;
    std::unordered_map<ActionTypeId,effects::EffectDefinitionId> mappings_;
    bool mappings_frozen_ = false;
    std::uint64_t mapping_revision_ = 0;
    static constexpr std::size_t kDeliveryCapacity = 4096;
    std::vector<AbilityEffectDeliveryRecord> delivered_;
};

struct ConditionProgressionMapping
{
    conditions::ConditionTypeId condition{};
    progression::AttributeTypeId attribute{};
    progression::ModifierOperation operation=progression::ModifierOperation::FinalAdd;
    TypeId modifier_type{};
    std::int32_t priority=0;
    std::int64_t magnitude_scale_micro=1'000'000;
};
struct ConditionProgressionCheckpoint
{
    ChangeCursor cursor{};
    std::uint64_t mapping_revision = 0;
};
class ConditionProgressionAdapter
{
  public:
    ConditionProgressionAdapter(const conditions::ConditionService& conditions,progression::ProgressionService& progression) : conditions_(conditions),progression_(progression) {}
    [[nodiscard]] foundation::Result<void> AddMapping(ConditionProgressionMapping mapping);
    [[nodiscard]] foundation::Result<void> FreezeMappings();
    [[nodiscard]] std::uint64_t MappingRevision() const noexcept { return mapping_revision_; }
    [[nodiscard]] foundation::Result<void> ProcessChanges();
    [[nodiscard]] foundation::Result<void> ReconcileAll();
    [[nodiscard]] ChangeCursor Cursor() const noexcept { return cursor_; }
    void RestoreCursor(ChangeCursor cursor) noexcept { cursor_ = cursor; }
    [[nodiscard]] ConditionProgressionCheckpoint CaptureCheckpoint() const noexcept { return {cursor_,mapping_revision_}; }
    [[nodiscard]] foundation::Result<void> RestoreCheckpoint(ConditionProgressionCheckpoint checkpoint);
  private:
    [[nodiscard]] static GameplayObjectRef ConditionSource(conditions::ConditionInstanceId id) noexcept { return {conditions::ConditionService::Domain(),id.value}; }
    [[nodiscard]] foundation::Result<void> Project(const conditions::ConditionInstance& condition, GameplayContext context);
    const conditions::ConditionService& conditions_;
    progression::ProgressionService& progression_;
    std::vector<ConditionProgressionMapping> mappings_;
    bool mappings_frozen_ = false;
    std::uint64_t mapping_revision_ = 0;
    ChangeCursor cursor_{};
};

struct AbilityTimePendingTrigger
{
    ScheduleId trigger{};
    abilities::AbilityExecutionId execution{};
    GameplayTimePoint observed_at{};
    std::vector<abilities::AbilityOutput> outputs;
};
struct AbilityTimeCheckpoint
{
    ClockId clock{};
    ActionTypeId action{};
    std::vector<AbilityTimePendingTrigger> pending;
};
class AbilityTimeAdapter
{
  public:
    AbilityTimeAdapter(time::GameplayTimeService& time,abilities::AbilityService& abilities,ClockId clock,ActionTypeId action) : time_(time),abilities_(abilities),clock_(clock),action_(action) {}
    [[nodiscard]] foundation::Result<ScheduleId> ScheduleExecution(abilities::AbilityExecutionId execution);
    [[nodiscard]] foundation::Result<std::vector<abilities::AbilityOutput>> ProcessTrigger(const time::ScheduledTrigger& trigger);
    [[nodiscard]] const AbilityTimePendingTrigger* FindPendingOutputs(ScheduleId trigger) const noexcept;
    [[nodiscard]] foundation::Result<void> AcknowledgeOutputs(ScheduleId trigger);
    [[nodiscard]] AbilityTimeCheckpoint CaptureCheckpoint() const { return {clock_, action_, pending_}; }
    [[nodiscard]] foundation::Result<void> RestoreCheckpoint(AbilityTimeCheckpoint checkpoint);
    [[nodiscard]] std::size_t PendingOutputCount() const noexcept { return pending_.size(); }
    [[nodiscard]] bool HasPendingOutputsForExecution(abilities::AbilityExecutionId execution) const noexcept;
  private:
    time::GameplayTimeService& time_;
    abilities::AbilityService& abilities_;
    ClockId clock_{};
    ActionTypeId action_{};
    static constexpr std::size_t kPendingCapacity = 4096;
    std::vector<AbilityTimePendingTrigger> pending_;
};

class AbilityOutputDeliveryCoordinator
{
  public:
    AbilityOutputDeliveryCoordinator(abilities::AbilityService& abilities,
                                     AbilityTimeAdapter& time_adapter,
                                     AbilityEffectsDispatcher& effects_dispatcher) noexcept
        : abilities_(abilities), time_adapter_(time_adapter), effects_dispatcher_(effects_dispatcher) {}

    [[nodiscard]] foundation::Result<std::vector<effects::EffectExecutionResult>> DeliverPendingOutputs(
        ScheduleId trigger);
    std::uint64_t PruneSafeTerminalDeliveries(abilities::AbilityExecutionId execution) noexcept;

  private:
    abilities::AbilityService& abilities_;
    AbilityTimeAdapter& time_adapter_;
    AbilityEffectsDispatcher& effects_dispatcher_;
};

struct ProgressionRewardPayload { progression::ProgressionTrackId track{}; };
class ProgressionRewardHandler final : public loot::IRewardHandler
{
  public:
    explicit ProgressionRewardHandler(progression::ProgressionService& progression) : progression_(progression) {}
    [[nodiscard]] static constexpr loot::RewardTypeId StaticType() noexcept { return loot::RewardTypeId::FromString("framework.reward.progression"); }
    [[nodiscard]] static constexpr TypeId PayloadType() noexcept { return TypeId::FromString("framework.reward.progression.payload"); }
    [[nodiscard]] loot::RewardTypeId Type() const noexcept override { return StaticType(); }
    [[nodiscard]] foundation::Result<loot::RewardDeliveryStage> Prepare(const loot::RewardOperation& operation) override;
    void Commit(loot::RewardDeliveryStage& stage) noexcept override;
    void Cancel(loot::RewardDeliveryStage& stage) noexcept override;
  private:
    struct StagePayload { progression::ProgressionGrantReservationId reservation{}; };
    [[nodiscard]] static constexpr TypeId StagePayloadType() noexcept { return TypeId::FromString("framework.reward.progression.stage"); }
    progression::ProgressionService& progression_;
};

struct DeathRewardDeliveryKey
{
    std::uint64_t combat_sequence = 0;
    GameplayObjectRef subject{};
    loot::LootTableId table{};
    [[nodiscard]] constexpr auto operator<=>(const DeathRewardDeliveryKey&) const noexcept = default;
};
enum class DeathRewardDeliveryState { Generated, Pending };
struct DeathRewardDeliveryRecord
{
    DeathRewardDeliveryKey key{};
    loot::RewardExecutionId reward{};
    DeathRewardDeliveryState state = DeathRewardDeliveryState::Generated;
};
struct DeathRewardCheckpoint
{
    ChangeCursor cursor{};
    std::uint64_t mapping_revision = 0;
    std::vector<DeathRewardDeliveryRecord> deliveries;
};
class DeathRewardAdapter
{
  public:
    DeathRewardAdapter(const combat::CombatService& combat,loot::LootService& loot) : combat_(combat),loot_(loot) {}
    [[nodiscard]] foundation::Result<void> SetTable(GameplayObjectRef subject,loot::LootTableId table);
    [[nodiscard]] foundation::Result<void> FreezeMappings();
    [[nodiscard]] std::uint64_t MappingRevision() const noexcept { return mapping_revision_; }
    [[nodiscard]] foundation::Result<std::vector<loot::RewardExecutionId>> ProcessChanges();
    [[nodiscard]] ChangeCursor Cursor() const noexcept { return cursor_; }
    void RestoreCursor(ChangeCursor cursor) noexcept { cursor_ = cursor; }
    [[nodiscard]] DeathRewardCheckpoint CaptureCheckpoint() const;
    [[nodiscard]] foundation::Result<void> RestoreCheckpoint(DeathRewardCheckpoint checkpoint);
  private:
    const combat::CombatService& combat_;
    loot::LootService& loot_;
    std::unordered_map<GameplayObjectRef,loot::LootTableId> tables_;
    bool mappings_frozen_ = false;
    std::uint64_t mapping_revision_ = 0;
    static constexpr std::size_t kDeliveryCapacity = 4096;
    std::vector<DeathRewardDeliveryRecord> deliveries_;
    ChangeCursor cursor_{};
};
} // namespace epidemic::gameplay::integration

