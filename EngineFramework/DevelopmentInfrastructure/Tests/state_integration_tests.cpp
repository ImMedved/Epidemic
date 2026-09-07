#include "Epidemic/GameFramework/StateIntegration/state_adapters.h"

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::conditions;
using namespace epidemic::gameplay::effects;
using namespace epidemic::gameplay::entities;
using namespace epidemic::gameplay::facts;
using namespace epidemic::gameplay::materials;
using namespace epidemic::gameplay::queries;
using namespace epidemic::gameplay::state_integration;
using namespace epidemic::gameplay::time;

namespace
{
constexpr std::string_view kMaterialStimulusPayloadName = "framework.payload.material.stimulus.v1";
constexpr std::string_view kSubstanceExposurePayloadName = "framework.payload.material.exposure.v1";
constexpr std::string_view kConditionRemoveTypePayloadName = "framework.payload.condition.remove_type.v1";
constexpr std::string_view kEntityTagPayloadName = "framework.payload.entity.tag.v1";

class CountingCommitHandler final : public IEffectHandler
{
  public:
    CountingCommitHandler(EffectTypeId type, int& commits) : type_(type), commits_(commits) {}
    [[nodiscard]] EffectTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<EffectPrepareResult> Prepare(const EffectOperation&) const override
    {
        return foundation::Result<EffectPrepareResult>::Success({EffectPrepareDisposition::Accepted, {}});
    }
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(
        const EffectOperation&, const RegisteredEffectPayload&) noexcept override
    {
        ++commits_;
        return foundation::Result<EffectCommitResult>::Success({EffectCommitDisposition::Applied, {}});
    }

  private:
    EffectTypeId type_{};
    int& commits_;
};

class FailingCommitHandler final : public IEffectHandler
{
  public:
    explicit FailingCommitHandler(EffectTypeId type) : type_(type) {}
    [[nodiscard]] EffectTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<EffectPrepareResult> Prepare(const EffectOperation&) const override
    {
        return foundation::Result<EffectPrepareResult>::Success({EffectPrepareDisposition::Accepted, {}});
    }
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(
        const EffectOperation&, const RegisteredEffectPayload&) noexcept override
    {
        return foundation::Result<EffectCommitResult>::Failure(
            foundation::Error::Create("test.forced_commit_failure", "forced second operation failure"));
    }

  private:
    EffectTypeId type_{};
};

EffectRequest MakeRequest(EffectDefinitionId definition, GameplayObjectRef target, GameplayContext context)
{
    EffectRequest request;
    request.definition = definition;
    request.targets = {target};
    request.context = context;
    request.source = context.source;
    request.instigator = context.instigator;
    return request;
}
}

int main()
{
    GameplayTagRegistry tags;
    const auto wood_tag = tags.Register("material.wood");
    const auto oil_tag = tags.Register("substance.oil");
    const auto water_tag = tags.Register("substance.water");
    const auto periodic_tag = tags.Register("entity.periodic");
    const auto delayed_tag = tags.Register("entity.delayed");
    if (!wood_tag || !oil_tag || !water_tag || !periodic_tag || !delayed_tag) return 1;
    tags.Freeze();

    GameplayTimeService time;
    const auto clock = time.RegisterClock("test.clock.world");
    if (!clock || !time.SynchronizeClock(clock.Value(), GameplayTimePoint{0}, Revision{1})) return 2;

    EntityService entities;
    EntityArchetypeDefinition object_archetype;
    object_archetype.canonical_name = "test.entity.object";
    const auto archetype = entities.RegisterArchetype(object_archetype);
    if (!archetype) return 3;

    MaterialService materials;
    MaterialDefinition wood_definition;
    wood_definition.canonical_name = "test.material.wood";
    wood_definition.tags.Add(wood_tag.Value());
    const auto wood_id = materials.RegisterMaterial(std::move(wood_definition));
    MaterialDefinition stone_definition;
    stone_definition.canonical_name = "test.material.stone";
    const auto stone_id = materials.RegisterMaterial(std::move(stone_definition));

    SubstanceDefinition oil_definition;
    oil_definition.canonical_name = "test.substance.oil";
    oil_definition.phase = SubstancePhase::Liquid;
    oil_definition.tags.Add(oil_tag.Value());
    const auto oil_id = materials.RegisterSubstance(std::move(oil_definition));
    SubstanceDefinition water_definition;
    water_definition.canonical_name = "test.substance.water";
    water_definition.phase = SubstancePhase::Liquid;
    water_definition.tags.Add(water_tag.Value());
    const auto water_id = materials.RegisterSubstance(std::move(water_definition));
    const auto body_slot = materials.RegisterSlot("test.material_slot.body");

    const auto ignite_response = MaterialResponseTypeId::FromString("test.material_response.ignite");
    const auto extinguish_response = MaterialResponseTypeId::FromString("test.material_response.extinguish");

    MaterialReactionRule wood_ignition;
    wood_ignition.canonical_name = "test.reaction.wood_ignition";
    wood_ignition.stimulus = MaterialStimulusType::Heat;
    wood_ignition.required_material_tag = wood_tag.Value();
    wood_ignition.threshold_field = ReactionThresholdField::Temperature;
    wood_ignition.min_threshold_micro = 100;
    wood_ignition.response_type = ignite_response;
    wood_ignition.priority = 10;

    MaterialReactionRule oil_ignition;
    oil_ignition.canonical_name = "test.reaction.oil_ignition";
    oil_ignition.stimulus = MaterialStimulusType::Heat;
    oil_ignition.required_substance_tag = oil_tag.Value();
    oil_ignition.threshold_field = ReactionThresholdField::StimulusMagnitude;
    oil_ignition.min_threshold_micro = 20;
    oil_ignition.response_type = ignite_response;
    oil_ignition.priority = 20;

    MaterialReactionRule water_extinguish;
    water_extinguish.canonical_name = "test.reaction.water_extinguish";
    water_extinguish.stimulus = MaterialStimulusType::Moisture;
    water_extinguish.required_substance_tag = water_tag.Value();
    water_extinguish.threshold_field = ReactionThresholdField::StimulusMagnitude;
    water_extinguish.min_threshold_micro = 1;
    water_extinguish.response_type = extinguish_response;
    water_extinguish.priority = 20;

    if (!wood_id || !stone_id || !oil_id || !water_id || !body_slot || !materials.RegisterReaction(wood_ignition) ||
        !materials.RegisterReaction(oil_ignition) || !materials.RegisterReaction(water_extinguish)) return 4;

    ConditionService conditions;
    const auto periodic_action = ActionTypeId::FromString("test.action.burning.periodic");
    ConditionDefinition burning_definition;
    burning_definition.canonical_name = "test.condition.burning";
    burning_definition.stacking = ConditionStackingPolicy::RefreshDuration;
    burning_definition.default_duration = GameplayDuration{10};
    burning_definition.periodic_interval = GameplayDuration{2};
    burning_definition.clock = clock.Value();
    burning_definition.persistence = ConditionPersistencePolicy::Persistent;
    burning_definition.on_periodic = periodic_action;
    burning_definition.publish_fact = true;
    burning_definition.periodic_catch_up = PeriodicCatchUpPolicy::Aggregate;
    const auto burning = conditions.RegisterCondition(std::move(burning_definition));
    if (!burning) return 5;

    EffectService effects;
    EntityTargetStateProvider target_state(entities);
    MaterialResponseEffectRouter material_router;
    StateEffectAdapter effect_adapter(entities, materials, conditions, effects, tags, target_state, material_router);
    const auto builtins = effect_adapter.RegisterHandlers();
    if (!builtins) return 6;

    const auto condition_payload = EncodeConditionApplyEffect(ConditionApplyEffectData{burning.Value(), std::nullopt, {}});
    if (!material_router.Register(ignite_response, [condition_payload, type = builtins.Value().condition_apply](
            const MaterialResponse& response, const EffectOperation& parent) {
            EffectOperation operation;
            operation.type = type;
            operation.target = response.subject;
            operation.magnitude_micro = response.magnitude_micro == 0 ? parent.magnitude_micro : response.magnitude_micro;
            operation.payload = condition_payload;
            operation.context = parent.context;
            return foundation::Result<std::vector<EffectOperation>>::Success({std::move(operation)});
        })) return 7;

    const auto remove_payload = RegisteredEffectPayload::FromTrivial(
        TypeId::FromString(kConditionRemoveTypePayloadName), ConditionRemoveTypeEffectPayload{burning.Value()});
    if (!material_router.Register(extinguish_response, [remove_payload, type = builtins.Value().condition_remove_type](
            const MaterialResponse& response, const EffectOperation& parent) {
            EffectOperation operation;
            operation.type = type;
            operation.target = response.subject;
            operation.magnitude_micro = 1;
            operation.payload = remove_payload;
            operation.context = parent.context;
            return foundation::Result<std::vector<EffectOperation>>::Success({std::move(operation)});
        })) return 8;

    EffectDefinition heat_definition;
    heat_definition.canonical_name = "test.effect.heat";
    heat_definition.steps.push_back(EffectStepDefinition{
        builtins.Value().material_stimulus,
        EffectTargetSelector::AllTargets,
        60,
        RegisteredEffectPayload::FromTrivial(TypeId::FromString(kMaterialStimulusPayloadName),
                                             MaterialStimulusEffectPayload{body_slot.Value(), MaterialStimulusType::Heat})});
    const auto heat = effects.RegisterDefinition(std::move(heat_definition));

    EffectDefinition add_oil_definition;
    add_oil_definition.canonical_name = "test.effect.add_oil";
    add_oil_definition.steps.push_back(EffectStepDefinition{
        builtins.Value().material_add_exposure,
        EffectTargetSelector::AllTargets,
        1000,
        RegisteredEffectPayload::FromTrivial(TypeId::FromString(kSubstanceExposurePayloadName),
                                             SubstanceExposureEffectPayload{body_slot.Value(), oil_id.Value(), 1'000'000})});
    const auto add_oil = effects.RegisterDefinition(std::move(add_oil_definition));

    EffectDefinition water_definition_effect;
    water_definition_effect.canonical_name = "test.effect.water";
    water_definition_effect.steps.push_back(EffectStepDefinition{
        builtins.Value().material_add_exposure,
        EffectTargetSelector::AllTargets,
        1000,
        RegisteredEffectPayload::FromTrivial(TypeId::FromString(kSubstanceExposurePayloadName),
                                             SubstanceExposureEffectPayload{body_slot.Value(), water_id.Value(), 1'000'000})});
    water_definition_effect.steps.push_back(EffectStepDefinition{
        builtins.Value().material_stimulus,
        EffectTargetSelector::AllTargets,
        1,
        RegisteredEffectPayload::FromTrivial(TypeId::FromString(kMaterialStimulusPayloadName),
                                             MaterialStimulusEffectPayload{body_slot.Value(), MaterialStimulusType::Moisture})});
    const auto water_effect = effects.RegisterDefinition(std::move(water_definition_effect));

    EffectDefinition periodic_tag_definition;
    periodic_tag_definition.canonical_name = "test.effect.periodic_tag";
    periodic_tag_definition.steps.push_back(EffectStepDefinition{
        builtins.Value().entity_add_tag,
        EffectTargetSelector::AllTargets,
        1,
        RegisteredEffectPayload::FromTrivial(TypeId::FromString(kEntityTagPayloadName), EntityTagEffectPayload{periodic_tag.Value()})});
    const auto periodic_tag_effect = effects.RegisterDefinition(std::move(periodic_tag_definition));

    EffectDefinition delayed_tag_definition;
    delayed_tag_definition.canonical_name = "test.effect.delayed_tag";
    delayed_tag_definition.steps.push_back(EffectStepDefinition{
        builtins.Value().entity_add_tag,
        EffectTargetSelector::AllTargets,
        1,
        RegisteredEffectPayload::FromTrivial(TypeId::FromString(kEntityTagPayloadName), EntityTagEffectPayload{delayed_tag.Value()})});
    const auto delayed_tag_effect = effects.RegisterDefinition(std::move(delayed_tag_definition));
    if (!heat || !add_oil || !water_effect || !periodic_tag_effect || !delayed_tag_effect) return 9;

    GameplayFactsService facts;
    GameplayQueryService queries;
    StateFactsAdapter facts_adapter(entities, materials, conditions, effects, facts);
    StateQueryAdapter query_adapter(entities, materials, conditions, tags, queries);
    StateTimeAdapter time_adapter(conditions, effects, time);
    epidemic::gameplay::integration::ScheduledTriggerDispatcher trigger_dispatcher(time);
    StateLifecycleAdapter lifecycle_adapter(entities, materials, conditions, effects);
    ConditionEffectsAdapter condition_effects(conditions, effects);

    if (!facts_adapter.RegisterContracts() || !query_adapter.RegisterProviders() || !time_adapter.RegisterContracts() ||
        !time_adapter.RegisterWithDispatcher(trigger_dispatcher) || !condition_effects.RegisterRoute(periodic_action, periodic_tag_effect.Value())) return 10;

    entities.Freeze();
    materials.Freeze();
    conditions.Freeze();
    effects.Freeze();
    facts.Freeze();
    queries.Freeze();
    time.Freeze();
    trigger_dispatcher.Freeze();

    GameplayContext context;
    context.tick = GameplayTickId{1};
    context.time = GameplayTimePoint{0};
    context.correlation = CorrelationId::FromString("test.integration.correlation");

    CreateEntityRequest create;
    create.archetype = archetype.Value();
    create.context = context;
    const auto house_created = entities.Create(create);
    if (!house_created) return 11;
    const auto house_id = house_created.Value().id;
    const auto house = EntityService::ToGameplayObjectRef(house_id);
    if (!materials.AssignComposition(house, body_slot.Value(), MaterialComposition{{MaterialConstituent{wood_id.Value(), 1'000'000}}}, context)) return 12;

    auto first_heat = effects.Execute(MakeRequest(heat.Value(), house, context));
    if (!first_heat || conditions.HasCondition(house, burning.Value())) return 13;
    auto second_heat = effects.Execute(MakeRequest(heat.Value(), house, context));
    if (!second_heat || second_heat.Value().waves != 2 || !conditions.HasCondition(house, burning.Value())) return 14;

    if (!time_adapter.SynchronizeConditionSchedules(context)) return 15;
    const auto active_conditions = conditions.GetConditions(house);
    if (active_conditions.size() != 1 || !active_conditions.front().periodic_schedule.has_value() ||
        !active_conditions.front().expiration_schedule.has_value()) return 16;

    const auto restored_expiration_id = *active_conditions.front().expiration_schedule;
    const auto restored_periodic_id = *active_conditions.front().periodic_schedule;
    const auto conditions_before_restore = conditions.CaptureSnapshot();
    const auto time_before_restore = time.CaptureSnapshot();
    if (!conditions.RestoreSnapshot(conditions_before_restore) || !time.RestoreSnapshot(time_before_restore) ||
        !time_adapter.SynchronizeConditionSchedules(context)) return 160;
    const auto rebound_conditions = conditions.GetConditions(house);
    if (rebound_conditions.size() != 1 ||
        rebound_conditions.front().expiration_schedule != restored_expiration_id ||
        rebound_conditions.front().periodic_schedule != restored_periodic_id ||
        time.CaptureSnapshot().schedules.size() != time_before_restore.schedules.size()) return 161;

    const auto published = facts_adapter.PublishPendingChanges(context);
    if (!published || published.Value() == 0 || !facts.Dispatch()) return 17;
    const FactKey burning_fact{facts_adapter.ActiveConditionFact(), house,
                               GameplayObjectRef{ConditionService::Domain(), active_conditions.front().id.value}};
    if (!facts.FindFactValueCopy<ConditionFactValue>(burning_fact).has_value()) return 18;

    const auto entity_query = queries.Execute(GetEntityQuery{house_id}, QueryContext{});
    const auto condition_query = queries.Execute(GetConditionsQuery{house}, QueryContext{});
    if (!entity_query || !entity_query.Value().value || !entity_query.Value().value->has_value() || !condition_query || !condition_query.Value().value || condition_query.Value().value->size() != 1) return 19;

    context.tick = GameplayTickId{2};
    context.time = GameplayTimePoint{3};
    if (!time.SynchronizeClock(clock.Value(), context.time, Revision{2})) return 20;
    const auto due = time_adapter.ProcessDue(trigger_dispatcher, clock.Value(), context);
    if (!due || due.Value().condition_periodic == 0) return 21;
    const auto periodic_effects = condition_effects.ProcessPending();
    const auto house_after_periodic = entities.Find(house_id);
    if (!periodic_effects || periodic_effects.Value().empty() || !house_after_periodic ||
        !house_after_periodic->instance_tags.HasExact(periodic_tag.Value())) return 22;

    const auto condition_effect_checkpoint = condition_effects.CaptureCheckpoint();
    const auto executions_before_condition_restore = effects.GetDiagnostics().executions;
    ConditionEffectsAdapter restored_condition_effects(conditions, effects);
    if (!restored_condition_effects.RegisterRoute(periodic_action, periodic_tag_effect.Value()) ||
        !restored_condition_effects.RestoreCheckpoint(condition_effect_checkpoint)) return 162;
    const auto replayed_condition_effects = restored_condition_effects.ProcessPending();
    if (!replayed_condition_effects || !replayed_condition_effects.Value().empty() ||
        effects.GetDiagnostics().executions != executions_before_condition_restore) return 163;

    auto water_result = effects.Execute(MakeRequest(water_effect.Value(), house, context));
    if (!water_result || conditions.HasCondition(house, burning.Value())) return 23;
    if (!time_adapter.SynchronizeConditionSchedules(context)) return 24;
    if (!facts_adapter.PublishPendingChanges(context) || !facts.Dispatch()) return 25;
    if (facts.FindFact(burning_fact).has_value()) return 26;

    CreateEntityRequest stone_create;
    stone_create.archetype = archetype.Value();
    stone_create.context = context;
    const auto stone_created = entities.Create(stone_create);
    if (!stone_created) return 27;
    const auto stone_ref = EntityService::ToGameplayObjectRef(stone_created.Value().id);
    if (!materials.AssignComposition(stone_ref, body_slot.Value(), MaterialComposition{{MaterialConstituent{stone_id.Value(), 1'000'000}}}, context)) return 28;
    if (!effects.Execute(MakeRequest(add_oil.Value(), stone_ref, context)) || !effects.Execute(MakeRequest(heat.Value(), stone_ref, context)) ||
        !conditions.HasCondition(stone_ref, burning.Value())) return 29;

    EffectRequest delayed_request = MakeRequest(delayed_tag_effect.Value(), house, context);
    const auto deferred = effects.Defer(std::move(delayed_request), clock.Value(), GameplayTimePoint{5}, DeferredEffectPersistence::Persistent);
    if (!deferred || !time_adapter.SynchronizeDeferredEffects(context)) return 30;
    const auto* deferred_record = effects.FindDeferred(deferred.Value());
    if (deferred_record == nullptr || !deferred_record->schedule.has_value()) return 31;

    const auto original_deferred_schedule = *deferred_record->schedule;
    if (!time.Cancel(original_deferred_schedule)) return 32;
    if (!time_adapter.SynchronizeDeferredEffects(context)) return 33;
    deferred_record = effects.FindDeferred(deferred.Value());
    if (deferred_record == nullptr || !deferred_record->schedule.has_value() ||
        *deferred_record->schedule == original_deferred_schedule || !time.HasSchedule(*deferred_record->schedule)) return 34;
    const auto peeked_deferred = effects.PeekDeferredBySchedule(*deferred_record->schedule, context);
    if (!peeked_deferred || effects.FindDeferred(deferred.Value()) == nullptr) return 35;

    context.tick = GameplayTickId{3};
    context.time = GameplayTimePoint{5};
    if (!time.SynchronizeClock(clock.Value(), context.time, Revision{3})) return 36;
    EffectExecutionBudget zero_effect_budget;
    zero_effect_budget.max_effects = 0;
    const auto failed_delayed_due = time_adapter.ProcessDue(trigger_dispatcher, clock.Value(), context, {}, zero_effect_budget);
    const auto house_after_failed_delayed = entities.Find(house_id);
    deferred_record = effects.FindDeferred(deferred.Value());
    if (!failed_delayed_due || trigger_dispatcher.PendingCount() != 1 || deferred_record == nullptr || !deferred_record->schedule.has_value() ||
        time.HasSchedule(*deferred_record->schedule) || !house_after_failed_delayed ||
        house_after_failed_delayed->instance_tags.HasExact(delayed_tag.Value())) return 37;

    const auto delayed_due = time_adapter.ProcessDue(trigger_dispatcher, clock.Value(), context);
    const auto house_after_delayed = entities.Find(house_id);
    if (!delayed_due || delayed_due.Value().effect_executions.size() != 1 || effects.FindDeferred(deferred.Value()) != nullptr ||
        !house_after_delayed || !house_after_delayed->instance_tags.HasExact(delayed_tag.Value())) return 38;

    GameplayFactsService gap_facts;
    StateFactsAdapter gap_facts_adapter(entities, materials, conditions, effects, gap_facts);
    if (!gap_facts_adapter.RegisterContracts()) return 39;
    gap_facts.Freeze();
    if (!gap_facts_adapter.PublishPendingChanges(context) || !gap_facts.Dispatch()) return 40;
    ApplyConditionRequest gap_apply_request;
    gap_apply_request.subject = house;
    gap_apply_request.type = burning.Value();
    gap_apply_request.magnitude_micro = 1;
    gap_apply_request.context = context;
    const auto gap_applied = conditions.Apply(gap_apply_request);
    if (!gap_applied) return 41;
    conditions.PruneChangesBefore(conditions.LatestChangeSequence() + 1);
    const auto gap_published = gap_facts_adapter.PublishPendingChanges(context);
    if (!gap_published || gap_published.Value() == 0 || !gap_facts.Dispatch()) return 42;
    const FactKey gap_fact{gap_facts_adapter.ActiveConditionFact(), house,
                           GameplayObjectRef{ConditionService::Domain(), gap_applied.Value().instance.value}};
    if (!gap_facts.FindFactValueCopy<ConditionFactValue>(gap_fact).has_value()) return 43;
    if (!conditions.Remove(gap_applied.Value().instance, ConditionRemovalReason::SystemCleanup, context)) return 44;

    // Lifecycle cleanup is routed through the adapter; no major calls its peers directly.
    const auto cleanup_deferred = effects.Defer(MakeRequest(delayed_tag_effect.Value(), stone_ref, context), clock.Value(), GameplayTimePoint{20},
                                                DeferredEffectPersistence::Persistent);
    if (!cleanup_deferred || !time_adapter.SynchronizeDeferredEffects(context)) return 45;
    if (!entities.RequestDestroy(stone_created.Value().id, EntityDestroyReason::Destroyed, context)) return 46;
    const auto destroyed = entities.CommitPendingDestruction();
    entities.PruneChangesBefore(entities.LatestChangeSequence() + 1);
    if (!destroyed || destroyed.Value().size() != 1 || !lifecycle_adapter.ProcessEntityChanges(context)) return 47;
    if (!materials.FindSlots(stone_ref).empty() || !conditions.GetConditions(stone_ref).empty() ||
        effects.FindDeferred(cleanup_deferred.Value()) != nullptr) return 48;

    if (!time_adapter.SynchronizeConditionSchedules(context) || !time_adapter.SynchronizeDeferredEffects(context)) return 49;


    const auto entity_snapshot = entities.CaptureSnapshot();
    const auto material_snapshot = materials.CaptureSnapshot();
    const auto condition_snapshot = conditions.CaptureSnapshot();
    const auto effect_snapshot = effects.CaptureSnapshot();
    if (entity_snapshot.records.empty() || material_snapshot.states.empty() || condition_snapshot.instances.size() != 0 ||
        !effect_snapshot.deferred.empty()) return 50;

    return 0;
}
