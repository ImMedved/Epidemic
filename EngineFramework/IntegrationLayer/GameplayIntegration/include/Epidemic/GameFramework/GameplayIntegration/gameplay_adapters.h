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
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(const effects::EffectOperation& operation,const effects::RegisteredEffectPayload& token) override;
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
    void AddMapping(ProgressionCombatMapping mapping) { mappings_.push_back(mapping); }
    [[nodiscard]] std::vector<combat::CombatModifier> Collect(const combat::DamageRequest& request) const override;
    [[nodiscard]] Revision RevisionFor(const combat::DamageRequest& request) const noexcept override;
  private:
    const progression::ProgressionService& progression_;
    std::vector<ProgressionCombatMapping> mappings_;
};

struct CombatAbilityResourceMapping
{
    abilities::AbilityResourceTypeId ability{};
    combat::CombatResourceTypeId combat{};
};
class CombatAbilityResourceProvider final : public abilities::IAbilityResourceProvider
{
  public:
    explicit CombatAbilityResourceProvider(combat::CombatService& combat);
    void AddMapping(CombatAbilityResourceMapping mapping) { mappings_[mapping.ability]=mapping.combat; }
    [[nodiscard]] bool CanAfford(GameplayObjectRef owner,abilities::AbilityResourceTypeId type,std::int64_t amount_micro) const override;
    [[nodiscard]] foundation::Result<abilities::AbilityResourceReservation> Reserve(GameplayObjectRef owner,abilities::AbilityResourceTypeId type,std::int64_t amount_micro,GameplayContext context) override;
    void Commit(const abilities::AbilityResourceReservation& reservation,GameplayContext context) noexcept override;
    void Release(const abilities::AbilityResourceReservation& reservation,GameplayContext context) noexcept override;
  private:
    struct ReservationToken { GameplayObjectRef owner{}; combat::CombatResourceTypeId resource{}; std::int64_t amount_micro=0; };
    static constexpr TypeId TokenType() noexcept { return TypeId::FromString("framework.abilities.combat_resource.reservation"); }
    combat::CombatService& combat_;
    std::unordered_map<abilities::AbilityResourceTypeId,combat::CombatResourceTypeId,abilities::IdHash> mappings_;
    std::unordered_map<GameplayObjectId, ReservationToken> active_reservations_;
    MonotonicIdGenerator<GameplayObjectId> reservation_ids_;
};

class AbilityEffectsDispatcher
{
  public:
    explicit AbilityEffectsDispatcher(effects::EffectService& effects) : effects_(effects) {}
    void Map(ActionTypeId action,effects::EffectDefinitionId definition) { mappings_[action]=definition; }
    [[nodiscard]] foundation::Result<std::vector<effects::EffectExecutionResult>> Dispatch(std::span<const abilities::AbilityOutput> outputs);
  private:
    effects::EffectService& effects_;
    std::unordered_map<ActionTypeId,effects::EffectDefinitionId> mappings_;
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
class ConditionProgressionAdapter
{
  public:
    ConditionProgressionAdapter(const conditions::ConditionService& conditions,progression::ProgressionService& progression) : conditions_(conditions),progression_(progression) {}
    void AddMapping(ConditionProgressionMapping mapping) { mappings_.push_back(mapping); }
    [[nodiscard]] foundation::Result<void> ProcessChanges();
    [[nodiscard]] std::uint64_t Cursor() const noexcept { return cursor_; }
    void RestoreCursor(std::uint64_t cursor) noexcept { cursor_ = cursor; }
  private:
    [[nodiscard]] static GameplayObjectRef ConditionSource(conditions::ConditionInstanceId id) noexcept { return {conditions::ConditionService::Domain(),id.value}; }
    const conditions::ConditionService& conditions_;
    progression::ProgressionService& progression_;
    std::vector<ConditionProgressionMapping> mappings_;
    std::uint64_t cursor_=0;
};

class AbilityTimeAdapter
{
  public:
    AbilityTimeAdapter(time::GameplayTimeService& time,abilities::AbilityService& abilities,ClockId clock,ActionTypeId action) : time_(time),abilities_(abilities),clock_(clock),action_(action) {}
    [[nodiscard]] foundation::Result<ScheduleId> ScheduleExecution(abilities::AbilityExecutionId execution);
    [[nodiscard]] foundation::Result<std::vector<abilities::AbilityOutput>> ProcessTrigger(const time::ScheduledTrigger& trigger);
  private:
    time::GameplayTimeService& time_;
    abilities::AbilityService& abilities_;
    ClockId clock_{};
    ActionTypeId action_{};
};

struct ProgressionRewardPayload { progression::ProgressionTrackId track{}; };
class ProgressionRewardHandler final : public loot::IRewardHandler
{
  public:
    explicit ProgressionRewardHandler(progression::ProgressionService& progression) : progression_(progression) {}
    [[nodiscard]] static constexpr loot::RewardTypeId StaticType() noexcept { return loot::RewardTypeId::FromString("framework.reward.progression"); }
    [[nodiscard]] static constexpr TypeId PayloadType() noexcept { return TypeId::FromString("framework.reward.progression.payload"); }
    [[nodiscard]] loot::RewardTypeId Type() const noexcept override { return StaticType(); }
    [[nodiscard]] foundation::Result<loot::RewardDeliveryDisposition> Validate(const loot::RewardOperation& operation) const override;
    [[nodiscard]] foundation::Result<loot::RewardDeliveryStage> Prepare(const loot::RewardOperation& operation) override;
    void Commit(loot::RewardDeliveryStage& stage) noexcept override;
    void Cancel(loot::RewardDeliveryStage& stage) noexcept override;
  private:
    struct StagePayload { progression::ProgressionGrantReservationId reservation{}; };
    [[nodiscard]] static constexpr TypeId StagePayloadType() noexcept { return TypeId::FromString("framework.reward.progression.stage"); }
    progression::ProgressionService& progression_;
};

class DeathRewardAdapter
{
  public:
    DeathRewardAdapter(const combat::CombatService& combat,loot::LootService& loot) : combat_(combat),loot_(loot) {}
    void SetTable(GameplayObjectRef subject,loot::LootTableId table) { tables_[subject]=table; }
    [[nodiscard]] foundation::Result<std::vector<loot::RewardExecutionId>> ProcessChanges();
    [[nodiscard]] std::uint64_t Cursor() const noexcept { return cursor_; }
    void RestoreCursor(std::uint64_t cursor) noexcept { cursor_ = cursor; }
  private:
    const combat::CombatService& combat_;
    loot::LootService& loot_;
    std::unordered_map<GameplayObjectRef,loot::LootTableId> tables_;
    std::uint64_t cursor_=0;
};
} // namespace epidemic::gameplay::integration
