#include "Epidemic/GameFramework/GameplayIntegration/gameplay_adapters.h"

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
    condition_adapter.AddMapping({buff_id.Value(), attack_id.Value(), ModifierOperation::FinalAdd, TypeId::FromString("game.mod.attack_buff"), 0, 1'000'000});
    ApplyConditionRequest buff_request;
    buff_request.type = buff_id.Value();
    buff_request.subject = player;
    buff_request.magnitude_micro = 2'000'000;
    if (!conditions.Apply(buff_request) || !condition_adapter.ProcessChanges()) return 4;
    auto boosted = progression.GetAttribute(player, attack_id.Value());
    if (!boosted || boosted.Value() != 7'000'000) return 5;

    CombatService combat;
    CombatResourceDefinition health;
    health.canonical_name = "game.health";
    health.default_maximum_micro = 100'000'000;
    health.drives_life_state = true;
    auto health_id = combat.RegisterResource(health);
    CombatResourceDefinition energy;
    energy.canonical_name = "game.energy";
    energy.default_maximum_micro = 50'000'000;
    auto energy_id = combat.RegisterResource(energy);
    auto damage_type = combat.RegisterDamageType("game.damage.physical");
    if (!health_id || !energy_id || !damage_type) return 6;
    DamageProfile profile;
    profile.canonical_name = "game.damage.basic";
    profile.target_resource = health_id.Value();
    profile.can_miss = false;
    profile.can_evade = false;
    profile.can_block = false;
    profile.can_critical = false;
    auto profile_id = combat.RegisterDamageProfile(profile);
    if (!profile_id) return 7;
    combat.Freeze();
    if (!combat.RegisterCombatant(player, {{health_id.Value(), 100'000'000, 100'000'000}, {energy_id.Value(), 50'000'000, 50'000'000}})) return 8;
    if (!combat.RegisterCombatant(enemy, {{health_id.Value(), 20'000'000, 20'000'000}})) return 9;

    ProgressionCombatModifierProvider modifier_provider(progression);
    modifier_provider.AddMapping({attack_id.Value(), ProgressionCombatRole::Attacker, CombatModifierPhase::Attacker, CombatModifierOperation::Add, CombatModifierTypeId::FromString("game.attack_bonus"), 0, 1'000'000});
    combat.SetModifierProvider(&modifier_provider);

    EffectService effects;
    auto damage_handler = std::make_shared<CombatDamageEffectHandler>(combat);
    auto registered_handler = effects.RegisterHandler(
        "framework.combat.damage",
        damage_handler,
        CombatDamageEffectHandler::PayloadType(),
        sizeof(CombatDamageEffectPayload));
    if (!registered_handler) return 10;
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
    if (!damage_effect_id) return 11;
    effects.Freeze();

    CombatAbilityResourceProvider ability_resources(combat);
    const auto mana = AbilityResourceTypeId::FromString("game.mana");
    ability_resources.AddMapping({mana, energy_id.Value()});

    AbilityService abilities;
    abilities.SetResourceProvider(&ability_resources);
    AbilityDefinition ability;
    ability.canonical_name = "game.ability.power_strike";
    ability.targeting = AbilityTargetPolicy::SingleTarget;
    ability.timing.kind = AbilityTimingKind::CastTime;
    ability.timing.cast_duration = {5};
    ability.costs.push_back({mana, 10'000'000, AbilityCostPolicy::ReserveThenCommit});
    const auto attack_action = ActionTypeId::FromString("game.ability.attack_effect");
    ability.outputs.push_back({attack_action, 1'000'000, {}});
    auto ability_def = abilities.RegisterDefinition(std::move(ability));
    if (!ability_def) return 12;
    abilities.Freeze();
    auto ability_instance = abilities.Grant(player, ability_def.Value());
    if (!ability_instance) return 13;

    GameplayTimeService gameplay_time;
    auto clock = gameplay_time.RegisterClock("game.world");
    auto ability_due_action = gameplay_time.RegisterAction("framework.abilities.execute", AbilityService::Domain());
    if (!clock || !ability_due_action) return 14;
    if (!gameplay_time.SynchronizeClock(clock.Value(), {0}, {1})) return 15;
    gameplay_time.Freeze();
    AbilityTimeAdapter ability_time(gameplay_time, abilities, clock.Value(), ability_due_action.Value());
    AbilityEffectsDispatcher ability_effects(effects);
    ability_effects.Map(attack_action, damage_effect_id.Value());

    AbilityTargetSet targets;
    targets.primary = enemy;
    GameplayContext cast_context;
    cast_context.actor = player;
    cast_context.instigator = player;
    cast_context.source = player;
    cast_context.operation = OperationId::FromString("test.cast.1");
    cast_context.correlation = CorrelationId::FromString("test.chain.1");
    auto execution = abilities.BeginActivation({ability_instance.Value(), targets, {0}, cast_context});
    if (!execution) return 16;
    auto schedule = ability_time.ScheduleExecution(execution.Value());
    if (!schedule) return 17;
    auto energy_after_reserve = combat.GetResource(player, energy_id.Value());
    if (!energy_after_reserve || energy_after_reserve.Value().current_micro != 40'000'000) return 18;
    if (!gameplay_time.SynchronizeClock(clock.Value(), {5}, {2})) return 19;
    auto triggers = gameplay_time.CollectDue(clock.Value());
    if (!triggers || triggers.Value().size() != 1) return 20;
    auto outputs = ability_time.ProcessTrigger(triggers.Value().front());
    if (!outputs || outputs.Value().size() != 1) return 21;
    auto effect_results = ability_effects.Dispatch(outputs.Value());
    if (!effect_results || effect_results.Value().size() != 1) return 22;
    const auto* enemy_state = combat.FindCombatant(enemy);
    if (enemy_state == nullptr || enemy_state->life_state != CombatLifeState::Dead) return 23;

    ProgressionRewardHandler progression_reward(progression);
    LootService loot;
    auto reward_type = loot.RegisterRewardHandler("framework.reward.progression", &progression_reward);
    if (!reward_type) return 24;
    RewardDefinition reward;
    reward.canonical_name = "game.reward.kill_xp";
    reward.type = reward_type.Value();
    reward.min_quantity_micro = 100;
    reward.max_quantity_micro = 100;
    reward.payload = RegisteredRewardPayload::FromTrivial(
        ProgressionRewardHandler::PayloadType(), ProgressionRewardPayload{xp_id.Value()});
    auto reward_id = loot.RegisterRewardDefinition(std::move(reward));
    if (!reward_id) return 25;
    LootTableDefinition table;
    table.canonical_name = "game.loot.enemy";
    table.policy = LootRollPolicy::GuaranteedAll;
    table.entries.push_back({LootEntryId::FromString("game.loot.enemy.xp"), 1, 1'000'000, reward_id.Value(), {}, {}});
    auto table_id = loot.RegisterLootTable(std::move(table));
    if (!table_id || !loot.Freeze()) return 26;
    DeathRewardAdapter death_rewards(combat, loot);
    death_rewards.SetTable(enemy, table_id.Value());
    auto generated = death_rewards.ProcessChanges();
    if (!generated || generated.Value().size() != 1) return 27;
    if (!loot.Claim(generated.Value().front(), cast_context)) return 28;
    auto xp_state = progression.GetTrack(player, xp_id.Value());
    if (!xp_state || xp_state.Value().progress_micro != 100 || xp_state.Value().rank != 1) return 29;

    return 0;
}
