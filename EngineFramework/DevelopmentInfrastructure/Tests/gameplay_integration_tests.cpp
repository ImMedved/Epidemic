#include "Epidemic/GameFramework/GameplayIntegration/gameplay_adapters.h"

#include <algorithm>
#include <memory>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::abilities;
using namespace epidemic::gameplay::combat;
using namespace epidemic::gameplay::conditions;
using namespace epidemic::gameplay::effects;
using namespace epidemic::gameplay::integration;
using namespace epidemic::gameplay::loot;
using namespace epidemic::gameplay::progression;
using namespace epidemic::gameplay::time;

namespace
{
[[nodiscard]] bool IsTerminal(AbilityExecutionState state) noexcept
{
    return state == AbilityExecutionState::Completed || state == AbilityExecutionState::Cancelled ||
           state == AbilityExecutionState::Interrupted || state == AbilityExecutionState::Failed;
}
class RetentionEffectHandler final : public IEffectHandler
{
public:
    [[nodiscard]] static constexpr EffectTypeId StaticType() noexcept
    {
        return EffectTypeId::FromString("test.retention.effect");
    }
    [[nodiscard]] EffectTypeId Type() const noexcept override { return StaticType(); }
    [[nodiscard]] EffectHandlerCapabilities Capabilities() const noexcept override { return {}; }
    [[nodiscard]] epidemic::foundation::Result<EffectPrepareResult> Prepare(const EffectOperation&) const override
    {
        return epidemic::foundation::Result<EffectPrepareResult>::Success({EffectPrepareDisposition::Accepted, {}});
    }
    [[nodiscard]] epidemic::foundation::Result<EffectCommitResult> Commit(
        const EffectOperation&, const RegisteredEffectPayload&) noexcept override
    {
        return epidemic::foundation::Result<EffectCommitResult>::Success({EffectCommitDisposition::Applied, {}});
    }
};
} // namespace

int main()
{
    const GameplayObjectRef player{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("player")};
    const GameplayObjectRef enemy{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("enemy")};

    ProgressionService progression;
    AttributeDefinition attack;
    attack.canonical_name = "game.attack_bonus";
    attack.min_micro = 0;
    attack.max_micro = 100'000'000;
    auto attack_id = progression.RegisterAttribute(attack);
    ProgressionTrackDefinition xp;
    xp.canonical_name = "game.xp";
    xp.rank_thresholds_micro = {100};
    auto xp_id = progression.RegisterTrack(xp);
    if (!attack_id || !xp_id || !progression.Freeze()) return 1;
    if (!progression.EnsureProfile(player) || !progression.SetBaseAttribute(player, attack_id.Value(), 5'000'000)) return 2;

    ConditionService conditions;
    ConditionDefinition buff;
    buff.canonical_name = "game.condition.attack_buff";
    buff.stacking = ConditionStackingPolicy::ReplaceExisting;
    auto buff_id = conditions.RegisterCondition(buff);
    if (!buff_id) return 3;
    conditions.Freeze();

    ConditionProgressionAdapter condition_adapter(conditions, progression);
    if (!condition_adapter.AddMapping({buff_id.Value(), attack_id.Value(), ModifierOperation::FinalAdd,
                                       TypeId::FromString("game.mod.attack_buff"), 0, 1'000'000})) return 4;
    if (condition_adapter.AddMapping({buff_id.Value(), attack_id.Value(), ModifierOperation::FinalAdd,
                                      TypeId::FromString("game.mod.attack_buff"), 0, 1'000'000})) return 5;
    if (!condition_adapter.FreezeMappings()) return 6;
    if (condition_adapter.AddMapping({buff_id.Value(), attack_id.Value(), ModifierOperation::FinalAdd,
                                      TypeId::FromString("game.mod.after_freeze"), 0, 1'000'000})) return 7;

    ApplyConditionRequest buff_request;
    buff_request.type = buff_id.Value();
    buff_request.subject = player;
    buff_request.magnitude_micro = 2'000'000;
    if (!conditions.Apply(buff_request) || !condition_adapter.ProcessChanges()) return 8;
    auto boosted = progression.GetAttribute(player, attack_id.Value());
    if (!boosted || boosted.Value() != 7'000'000) return 9;

    // H59: an invalid replacement must not remove the already projected modifier.
    ConditionProgressionAdapter failing_condition_adapter(conditions, progression);
    if (!failing_condition_adapter.AddMapping(
            {buff_id.Value(), AttributeTypeId::FromString("game.attribute.missing"), ModifierOperation::FinalAdd,
             TypeId::FromString("game.mod.attack_buff.invalid"), 0, 1'000'000}) ||
        !failing_condition_adapter.FreezeMappings()) return 10;
    const auto failed_checkpoint = failing_condition_adapter.CaptureCheckpoint();
    if (failed_checkpoint.cursor.sequence != 0 || failed_checkpoint.mapping_revision == 0) return 11;
    auto failed_apply = failing_condition_adapter.ProcessChanges();
    if (failed_apply || failing_condition_adapter.Cursor().sequence != 0) return 12;
    boosted = progression.GetAttribute(player, attack_id.Value());
    if (!boosted || boosted.Value() != 7'000'000) return 13;
    auto restored_condition_checkpoint = failed_checkpoint;
    restored_condition_checkpoint.cursor.sequence = 17;
    if (!failing_condition_adapter.RestoreCheckpoint(restored_condition_checkpoint) ||
        failing_condition_adapter.Cursor().sequence != 17) return 14;

    CombatService combat;
    CombatResourceDefinition health;
    health.canonical_name = "game.health";
    health.default_maximum_micro = 100'000'000;
    health.depletion_effect = CombatResourceDepletionEffect::Dead;
    auto health_id = combat.RegisterResource(health);
    CombatResourceDefinition energy;
    energy.canonical_name = "game.energy";
    energy.default_maximum_micro = 50'000'000;
    auto energy_id = combat.RegisterResource(energy);
    auto damage_type = combat.RegisterDamageType("game.damage.physical");
    if (!health_id || !energy_id || !damage_type) return 15;
    DamageProfile profile;
    profile.canonical_name = "game.damage.basic";
    profile.target_resource = health_id.Value();
    profile.can_miss = false;
    profile.can_evade = false;
    profile.can_block = false;
    profile.can_critical = false;
    auto profile_id = combat.RegisterDamageProfile(profile);
    if (!profile_id) return 16;
    combat.Freeze();
    if (!combat.RegisterCombatant(player, {{health_id.Value(), 100'000'000, 100'000'000},
                                           {energy_id.Value(), 50'000'000, 50'000'000}})) return 17;
    if (!combat.RegisterCombatant(enemy, {{health_id.Value(), 20'000'000, 20'000'000}})) return 18;

    ProgressionCombatModifierProvider modifier_provider(progression);
    if (!modifier_provider.AddMapping({attack_id.Value(), ProgressionCombatRole::Attacker,
                                       CombatModifierPhase::Attacker, CombatModifierOperation::Add,
                                       CombatModifierTypeId::FromString("game.attack_bonus"), 0, 1'000'000}) ||
        !modifier_provider.FreezeMappings()) return 19;
    if (modifier_provider.MappingRevision() == 0) return 20;
    combat.SetModifierProvider(&modifier_provider);

    EffectService effects;
    auto damage_handler = std::make_shared<CombatDamageEffectHandler>(combat);
    auto registered_handler = effects.RegisterHandler(
        "framework.combat.damage", damage_handler, CombatDamageEffectHandler::PayloadType(),
        sizeof(CombatDamageEffectPayload));
    if (!registered_handler) return 21;
    EffectDefinition damage_effect;
    damage_effect.canonical_name = "game.effect.basic_attack";
    EffectStepDefinition damage_step;
    damage_step.type = CombatDamageEffectHandler::StaticType();
    damage_step.magnitude_micro = 20'000'000;
    damage_step.payload = RegisteredEffectPayload::FromTrivial(
        CombatDamageEffectHandler::PayloadType(),
        CombatDamageEffectPayload{damage_type.Value(), profile_id.Value(), 77});
    damage_effect.steps.push_back(damage_step);
    auto damage_effect_id = effects.RegisterDefinition(std::move(damage_effect));
    if (!damage_effect_id) return 22;
    effects.Freeze();

    CombatAbilityResourceProvider ability_resources(combat);
    const auto mana = AbilityResourceTypeId::FromString("game.mana");
    if (!ability_resources.AddMapping({mana, energy_id.Value()}) || !ability_resources.FreezeMappings()) return 23;
    if (ability_resources.AddMapping({AbilityResourceTypeId::FromString("game.other"), energy_id.Value()})) return 24;

    const auto attack_action = ActionTypeId::FromString("game.ability.attack_effect");
    AbilityDefinition ability;
    ability.canonical_name = "game.ability.power_strike";
    ability.targeting = AbilityTargetPolicy::SingleTarget;
    ability.timing.kind = AbilityTimingKind::CastTime;
    ability.timing.cast_duration = {5};
    ability.costs.push_back({mana, 10'000'000, AbilityCostPolicy::ReserveThenCommit});
    ability.outputs.push_back({attack_action, 1'000'000, {}});

    AbilityService abilities;
    abilities.SetResourceProvider(&ability_resources);
    auto ability_def = abilities.RegisterDefinition(ability);
    if (!ability_def) return 25;
    abilities.Freeze();
    auto ability_instance = abilities.Grant(player, ability_def.Value());
    if (!ability_instance) return 26;

    AbilityTargetSet targets;
    targets.primary = enemy;
    GameplayContext cast_context;
    cast_context.actor = player;
    cast_context.instigator = player;
    cast_context.source = player;
    cast_context.operation = OperationId::FromString("test.cast.1");
    cast_context.correlation = CorrelationId::FromString("test.chain.1");
    auto execution = abilities.BeginActivation({ability_instance.Value(), targets, {0}, cast_context});
    if (!execution) return 27;
    auto energy_after_reserve = combat.GetResource(player, energy_id.Value());
    if (!energy_after_reserve || energy_after_reserve.Value().current_micro != 40'000'000) return 28;

    // C14: both the tentative debit and the reservation token survive save/load in their authoritative owners.
    const auto combat_reserved_snapshot = combat.CaptureSnapshot();
    const auto abilities_reserved_snapshot = abilities.CaptureSnapshot();
    if (combat_reserved_snapshot.resource_reservations.size() != 1 ||
        abilities_reserved_snapshot.executions.size() != 1 ||
        abilities_reserved_snapshot.executions.front().reservations.size() != 1) return 29;

    CombatService restored_combat;
    auto restored_health = restored_combat.RegisterResource(health);
    auto restored_energy = restored_combat.RegisterResource(energy);
    if (!restored_health || !restored_energy) return 30;
    restored_combat.Freeze();
    if (!restored_combat.RestoreSnapshot(combat_reserved_snapshot)) return 31;
    CombatAbilityResourceProvider restored_resources(restored_combat);
    if (!restored_resources.AddMapping({mana, restored_energy.Value()}) || !restored_resources.FreezeMappings()) return 32;
    AbilityService restored_abilities;
    restored_abilities.SetResourceProvider(&restored_resources);
    auto restored_definition = restored_abilities.RegisterDefinition(ability);
    if (!restored_definition) return 33;
    restored_abilities.Freeze();
    if (!restored_abilities.RestoreSnapshot(abilities_reserved_snapshot) ||
        !restored_abilities.NeedsResourceReconciliation() ||
        !restored_abilities.ReconcileRestoredReservations()) return 34;
    if (!restored_abilities.Cancel(execution.Value(), {1}, cast_context)) return 35;
    auto restored_energy_state = restored_combat.GetResource(player, restored_energy.Value());
    if (!restored_energy_state || restored_energy_state.Value().current_micro != 50'000'000) return 36;

    GameplayTimeService gameplay_time;
    auto clock = gameplay_time.RegisterClock("game.world");
    auto ability_due_action = gameplay_time.RegisterAction("framework.abilities.execute", AbilityService::Domain());
    if (!clock || !ability_due_action) return 37;
    if (!gameplay_time.SynchronizeClock(clock.Value(), {0}, {1})) return 38;
    gameplay_time.Freeze();
    AbilityTimeAdapter ability_time(gameplay_time, abilities, clock.Value(), ability_due_action.Value());
    AbilityEffectsDispatcher ability_effects(effects);
    if (!ability_effects.Map(attack_action, damage_effect_id.Value()) || !ability_effects.FreezeMappings()) return 39;

    auto schedule = ability_time.ScheduleExecution(execution.Value());
    if (!schedule) return 40;
    if (!gameplay_time.SynchronizeClock(clock.Value(), {5}, {2})) return 41;
    auto triggers = gameplay_time.CollectDue(clock.Value());
    if (!triggers || triggers.Value().size() != 1) return 42;
    auto outputs = ability_time.ProcessTrigger(triggers.Value().front());
    if (!outputs || outputs.Value().size() != 1 || outputs.Value().front().occurrence_at.ticks != 5 ||
        outputs.Value().front().output_index != 0) return 43;
    const auto undelivered_outputs_checkpoint = ability_time.CaptureCheckpoint();
    AbilityTimeAdapter restored_ability_time(gameplay_time, abilities, clock.Value(), ability_due_action.Value());
    if (!restored_ability_time.RestoreCheckpoint(undelivered_outputs_checkpoint) ||
        restored_ability_time.PendingOutputCount() != 1) return 143;
    auto restored_time_outputs = restored_ability_time.ProcessTrigger(triggers.Value().front());
    if (!restored_time_outputs || restored_time_outputs.Value().size() != 1 ||
        restored_time_outputs.Value().front().execution != outputs.Value().front().execution ||
        restored_time_outputs.Value().front().output_index != outputs.Value().front().output_index) return 144;
    auto effect_results = ability_effects.Dispatch(restored_time_outputs.Value());
    if (!effect_results || effect_results.Value().size() != 1) return 44;
    const auto *enemy_state = combat.FindCombatant(enemy);
    if (enemy_state == nullptr || enemy_state->life_state != CombatLifeState::Dead) return 45;

    // H58: retrying the exact same output returns the same delivery without another Effect execution.
    const auto executions_after_first_dispatch = effects.GetDiagnostics().executions;
    auto duplicate_effect_results = ability_effects.Dispatch(outputs.Value());
    if (!duplicate_effect_results || duplicate_effect_results.Value().size() != 1 ||
        effects.GetDiagnostics().executions != executions_after_first_dispatch ||
        duplicate_effect_results.Value().front().execution != effect_results.Value().front().execution) return 46;
    const auto effects_checkpoint = ability_effects.CaptureCheckpoint();
    AbilityEffectsDispatcher restored_effect_dispatcher(effects);
    if (!restored_effect_dispatcher.Map(attack_action, damage_effect_id.Value()) ||
        !restored_effect_dispatcher.FreezeMappings() ||
        !restored_effect_dispatcher.RestoreCheckpoint(effects_checkpoint)) return 47;
    if (!restored_effect_dispatcher.Dispatch(outputs.Value()) ||
        effects.GetDiagnostics().executions != executions_after_first_dispatch) return 48;

    // M05: a terminal execution is still protected while AbilityTime can replay its pending outbox batch.
    AbilityOutputDeliveryCoordinator ability_delivery(abilities, restored_ability_time, ability_effects);
    if (ability_delivery.PruneSafeTerminalDeliveries(execution.Value()) != 0 ||
        ability_effects.CaptureCheckpoint().delivered.empty()) return 147;

    // Canonical delivery is Dispatch -> ACK -> safe terminal prune. Dispatch is memoized here.
    auto canonical_effect_results = ability_delivery.DeliverPendingOutputs(schedule.Value());
    if (!canonical_effect_results || canonical_effect_results.Value().size() != 1 ||
        restored_ability_time.PendingOutputCount() != 0 ||
        !restored_ability_time.CaptureCheckpoint().pending.empty() ||
        !ability_effects.CaptureCheckpoint().delivered.empty()) return 148;

    // H60: if the next channel schedule cannot be created after the semantic tick, outputs remain
    // in the adapter checkpoint and retry only repairs the missing binding.
    AbilityService channel_abilities;
    AbilityDefinition channel_definition;
    channel_definition.canonical_name = "game.ability.channel";
    channel_definition.targeting = AbilityTargetPolicy::Self;
    channel_definition.timing.kind = AbilityTimingKind::Channel;
    channel_definition.timing.channel_interval = {5};
    channel_definition.timing.max_channel_duration = {15};
    channel_definition.outputs.push_back({attack_action, 1'000'000, {}});
    auto channel_def_id = channel_abilities.RegisterDefinition(channel_definition);
    if (!channel_def_id) return 49;
    channel_abilities.Freeze();
    auto channel_instance = channel_abilities.Grant(player, channel_def_id.Value());
    if (!channel_instance) return 50;
    AbilityTargetSet self_targets;
    self_targets.primary = player;
    auto channel_execution = channel_abilities.BeginActivation({channel_instance.Value(), self_targets, {0}, cast_context});
    if (!channel_execution) return 51;

    GameplayTimeService channel_time;
    auto channel_clock = channel_time.RegisterClock("game.channel.world");
    auto channel_action = channel_time.RegisterAction("framework.abilities.channel.execute", AbilityService::Domain());
    if (!channel_clock || !channel_action || !channel_time.SynchronizeClock(channel_clock.Value(), {0}, {1})) return 52;
    channel_time.Freeze();
    AbilityTimeAdapter channel_adapter(channel_time, channel_abilities, channel_clock.Value(), channel_action.Value());
    auto channel_schedule = channel_adapter.ScheduleExecution(channel_execution.Value());
    if (!channel_schedule || !channel_time.SynchronizeClock(channel_clock.Value(), {5}, {2})) return 53;
    auto channel_triggers = channel_time.CollectDue(channel_clock.Value());
    if (!channel_triggers || channel_triggers.Value().size() != 1) return 54;
    auto healthy_time_snapshot = channel_time.CaptureSnapshot();
    auto exhausted_time_snapshot = healthy_time_snapshot;
    exhausted_time_snapshot.schedule_ids.next = 0;
    if (!channel_time.RestoreSnapshot(exhausted_time_snapshot)) return 55;
    auto channel_failed = channel_adapter.ProcessTrigger(channel_triggers.Value().front());
    if (channel_failed) return 56;
    const auto channel_checkpoint = channel_adapter.CaptureCheckpoint();
    if (channel_checkpoint.pending.size() != 1 || channel_checkpoint.pending.front().outputs.size() != 1) return 57;
    const auto *channel_after_failure = channel_abilities.FindExecution(channel_execution.Value());
    if (!channel_after_failure || channel_after_failure->next_channel_at.ticks != 10 || channel_after_failure->schedule) return 58;
    if (!channel_time.RestoreSnapshot(healthy_time_snapshot)) return 59;
    auto channel_retry = channel_adapter.ProcessTrigger(channel_triggers.Value().front());
    if (!channel_retry || channel_retry.Value().size() != 1 || channel_retry.Value().front().occurrence_at.ticks != 5) return 60;
    AbilityOutputDeliveryCoordinator channel_delivery(channel_abilities, channel_adapter, ability_effects);
    auto channel_delivered = channel_delivery.DeliverPendingOutputs(channel_triggers.Value().front().schedule);
    if (!channel_delivered || channel_delivered.Value().size() != 1) return 146;
    const auto channel_effect_checkpoint = ability_effects.CaptureCheckpoint();
    if (channel_delivery.PruneSafeTerminalDeliveries(channel_execution.Value()) != 0 ||
        std::none_of(channel_effect_checkpoint.delivered.begin(), channel_effect_checkpoint.delivered.end(),
                     [&](const auto& record) { return record.key.execution == channel_execution.Value(); })) return 149;
    const auto *channel_after_retry = channel_abilities.FindExecution(channel_execution.Value());
    if (!channel_after_retry || !channel_after_retry->schedule || channel_after_retry->next_channel_at.ticks != 10) return 61;
    auto channel_duplicate = channel_adapter.ProcessTrigger(channel_triggers.Value().front());
    if (!channel_duplicate || !channel_duplicate.Value().empty() ||
        channel_abilities.FindExecution(channel_execution.Value())->next_channel_at.ticks != 10) return 62;

    // M05: >4096 sequential terminal executions through the canonical outbox path must not
    // exhaust the dispatcher delivery ledger.
    EffectService retention_effects;
    auto retention_handler = std::make_shared<RetentionEffectHandler>();
    auto retention_type = retention_effects.RegisterHandler("test.retention.effect", retention_handler);
    if (!retention_type) return 150;
    EffectDefinition retention_effect_definition;
    retention_effect_definition.canonical_name = "test.retention.definition";
    retention_effect_definition.steps.push_back({RetentionEffectHandler::StaticType(),
                                                 EffectTargetSelector::FirstTarget, 1, {}});
    auto retention_effect_id = retention_effects.RegisterDefinition(std::move(retention_effect_definition));
    if (!retention_effect_id) return 151;
    retention_effects.Freeze();

    const auto retention_output_action = ActionTypeId::FromString("test.retention.output");
    AbilityService retention_abilities;
    AbilityDefinition retention_ability_definition;
    retention_ability_definition.canonical_name = "test.retention.ability";
    retention_ability_definition.targeting = AbilityTargetPolicy::Self;
    retention_ability_definition.timing.kind = AbilityTimingKind::Instant;
    retention_ability_definition.outputs.push_back({retention_output_action, 1, {}});
    auto retention_ability_id = retention_abilities.RegisterDefinition(retention_ability_definition);
    if (!retention_ability_id) return 152;
    retention_abilities.Freeze();
    auto retention_instance = retention_abilities.Grant(player, retention_ability_id.Value());
    if (!retention_instance) return 153;

    GameplayTimeService retention_time;
    auto retention_clock = retention_time.RegisterClock("test.retention.clock");
    auto retention_due_action =
        retention_time.RegisterAction("test.retention.execute", AbilityService::Domain());
    if (!retention_clock || !retention_due_action) return 154;
    retention_time.Freeze();

    AbilityTimeAdapter retention_time_adapter(
        retention_time, retention_abilities, retention_clock.Value(), retention_due_action.Value());
    AbilityEffectsDispatcher retention_dispatcher(retention_effects);
    if (!retention_dispatcher.Map(retention_output_action, retention_effect_id.Value()) ||
        !retention_dispatcher.FreezeMappings()) return 155;
    AbilityOutputDeliveryCoordinator retention_delivery(
        retention_abilities, retention_time_adapter, retention_dispatcher);

    AbilityTargetSet retention_targets;
    retention_targets.primary = player;
    for (std::uint64_t i = 0; i < 4097; ++i)
    {
        GameplayContext retention_context;
        retention_context.actor = player;
        const GameplayTimePoint now{static_cast<std::int64_t>(i)};
        auto retention_execution = retention_abilities.BeginActivation(
            {retention_instance.Value(), retention_targets, now, retention_context});
        if (!retention_execution) return 156;
        auto retention_outputs = retention_abilities.CompleteExecution(retention_execution.Value(), now);
        const auto* completed_execution = retention_abilities.FindExecution(retention_execution.Value());
        if (!retention_outputs || retention_outputs.Value().size() != 1 ||
            (completed_execution && !IsTerminal(completed_execution->state)))
            return 157;

        const ScheduleId retention_trigger = ScheduleId::FromRaw(1, i + 1);
        AbilityTimeCheckpoint pending_checkpoint;
        pending_checkpoint.clock = retention_clock.Value();
        pending_checkpoint.action = retention_due_action.Value();
        pending_checkpoint.pending.push_back(
            {retention_trigger, retention_execution.Value(), now, retention_outputs.Value()});
        if (!retention_time_adapter.RestoreCheckpoint(std::move(pending_checkpoint)))
            return 158;
        auto delivered = retention_delivery.DeliverPendingOutputs(retention_trigger);
        if (!delivered || retention_time_adapter.PendingOutputCount() != 0 ||
            !retention_dispatcher.CaptureCheckpoint().delivered.empty())
            return 159;
    }

    ProgressionRewardHandler progression_reward(progression);
    LootService loot;
    auto reward_type = loot.RegisterRewardHandler("framework.reward.progression", &progression_reward);
    if (!reward_type) return 63;
    RewardDefinition reward;
    reward.canonical_name = "game.reward.kill_xp";
    reward.type = reward_type.Value();
    reward.min_quantity_micro = 100;
    reward.max_quantity_micro = 100;
    reward.payload = RegisteredRewardPayload::FromTrivial(
        ProgressionRewardHandler::PayloadType(), ProgressionRewardPayload{xp_id.Value()});
    auto reward_id = loot.RegisterRewardDefinition(std::move(reward));
    if (!reward_id) return 64;
    LootTableDefinition table;
    table.canonical_name = "game.loot.enemy";
    table.policy = LootRollPolicy::GuaranteedAll;
    table.entries.push_back({LootEntryId::FromString("game.loot.enemy.xp"), 1, 1'000'000, reward_id.Value(), {}, {}});
    auto table_id = loot.RegisterLootTable(std::move(table));
    if (!table_id || !loot.Freeze()) return 65;
    DeathRewardAdapter death_rewards(combat, loot);
    if (!death_rewards.SetTable(enemy, table_id.Value()) || !death_rewards.FreezeMappings()) return 66;
    auto generated = death_rewards.ProcessChanges();
    if (!generated || generated.Value().size() != 1) return 67;
    const auto reward_execution = generated.Value().front();
    const auto loot_bundles_after_first = loot.GetDiagnostics().bundles;

    // H61: replay before cursor acknowledgement reuses the same tracked reward execution.
    auto death_checkpoint = death_rewards.CaptureCheckpoint();
    death_checkpoint.cursor.sequence = 0;
    if (!death_rewards.RestoreCheckpoint(death_checkpoint)) return 68;
    auto replayed = death_rewards.ProcessChanges();
    if (!replayed || replayed.Value().size() != 1 || replayed.Value().front() != reward_execution ||
        loot.GetDiagnostics().bundles != loot_bundles_after_first) return 69;

    if (!loot.Claim(reward_execution, cast_context)) return 70;
    auto xp_state = progression.GetTrack(player, xp_id.Value());
    if (!xp_state || xp_state.Value().progress_micro != 100 || xp_state.Value().rank != 1) return 71;

    return 0;
}
