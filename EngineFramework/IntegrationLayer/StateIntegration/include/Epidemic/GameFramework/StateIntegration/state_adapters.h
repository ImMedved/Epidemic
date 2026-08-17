#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Conditions/conditions.h"
#include "Epidemic/GameFramework/Effects/effects.h"
#include "Epidemic/GameFramework/Entities/entities.h"
#include "Epidemic/GameFramework/Facts/gameplay_facts.h"
#include "Epidemic/GameFramework/Materials/materials.h"
#include "Epidemic/GameFramework/Queries/gameplay_queries.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"

#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::state_integration
{
struct GetEntityQuery
{
    using ResultType = std::optional<entities::EntityRecord>;
    entities::EntityId id{};
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.query.entities.get"); }
};

struct FindEntitiesQuery
{
    using ResultType = std::vector<entities::EntityRecord>;
    std::optional<entities::EntityArchetypeId> archetype{};
    std::optional<TagId> tag{};
    std::optional<entities::EntityLifecycleState> lifecycle{};
    std::optional<entities::EntityMaterializationState> materialization{};
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.query.entities.find"); }
};

struct GetMaterialSlotsQuery
{
    using ResultType = std::vector<materials::MaterialSlotState>;
    GameplayObjectRef subject{};
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.query.materials.slots"); }
};

struct GetConditionsQuery
{
    using ResultType = std::vector<conditions::ConditionInstance>;
    GameplayObjectRef subject{};
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.query.conditions.subject"); }
};

class StateQueryAdapter
{
  public:
    StateQueryAdapter(
        entities::EntityService& entities,
        materials::MaterialService& materials,
        conditions::ConditionService& conditions,
        const GameplayTagRegistry& tags,
        queries::GameplayQueryService& queries)
        : entities_(entities), materials_(materials), conditions_(conditions), tags_(tags), queries_(queries)
    {
    }

    [[nodiscard]] foundation::Result<void> RegisterProviders();

  private:
    entities::EntityService& entities_;
    materials::MaterialService& materials_;
    conditions::ConditionService& conditions_;
    const GameplayTagRegistry& tags_;
    queries::GameplayQueryService& queries_;
};

struct ConditionFactValue
{
    conditions::ConditionTypeId type{};
    std::int64_t magnitude_micro = 0;
    std::uint32_t stacks = 0;
    bool paused_for_materialization = false;
};

class StateFactsAdapter
{
  public:
    StateFactsAdapter(
        entities::EntityService& entities,
        materials::MaterialService& materials,
        conditions::ConditionService& conditions,
        effects::EffectService& effects,
        facts::GameplayFactsService& facts)
        : entities_(entities), materials_(materials), conditions_(conditions), effects_(effects), facts_(facts)
    {
    }

    [[nodiscard]] foundation::Result<void> RegisterContracts();
    [[nodiscard]] foundation::Result<std::uint64_t> PublishPendingChanges(GameplayContext context = {});

    [[nodiscard]] EventTypeId EntityChangedEvent() const noexcept { return entity_changed_; }
    [[nodiscard]] EventTypeId MaterialChangedEvent() const noexcept { return material_changed_; }
    [[nodiscard]] EventTypeId ConditionChangedEvent() const noexcept { return condition_changed_; }
    [[nodiscard]] EventTypeId EffectChangedEvent() const noexcept { return effect_changed_; }
    [[nodiscard]] FactTypeId ActiveConditionFact() const noexcept { return active_condition_fact_; }

  private:
    [[nodiscard]] GameplayObjectRef ConditionScope(conditions::ConditionInstanceId id) const noexcept
    {
        return GameplayObjectRef{conditions::ConditionService::Domain(), id.value};
    }

    entities::EntityService& entities_;
    materials::MaterialService& materials_;
    conditions::ConditionService& conditions_;
    effects::EffectService& effects_;
    facts::GameplayFactsService& facts_;

    EventTypeId entity_changed_{};
    EventTypeId material_changed_{};
    EventTypeId condition_changed_{};
    EventTypeId effect_changed_{};
    FactTypeId active_condition_fact_{};

    std::uint64_t entity_cursor_ = 0;
    std::uint64_t material_cursor_ = 0;
    std::uint64_t condition_cursor_ = 0;
    std::uint64_t effect_cursor_ = 0;
};

struct EntityCreateEffectPayload
{
    entities::EntityArchetypeId archetype{};
    entities::EntityPersistencePolicy persistence = entities::EntityPersistencePolicy::Persistent;
};
struct EntityConvertEffectPayload
{
    entities::EntityArchetypeId archetype{};
    bool preserve_instance_tags = true;
    bool preserve_persistence = true;
};
struct EntityTagEffectPayload
{
    TagId tag{};
};
struct MaterialStimulusEffectPayload
{
    materials::MaterialSlotId slot{};
    materials::MaterialStimulusType stimulus = materials::MaterialStimulusType::Heat;
};
struct SubstanceExposureEffectPayload
{
    materials::MaterialSlotId slot{};
    materials::SubstanceId substance{};
    std::uint32_t coverage_ppm = 0;
};
struct ConditionRemoveTypeEffectPayload
{
    conditions::ConditionTypeId type{};
};

struct ConditionApplyEffectData
{
    conditions::ConditionTypeId type{};
    std::optional<GameplayDuration> duration{};
    conditions::RegisteredConditionPayload condition_payload;
};

[[nodiscard]] effects::RegisteredEffectPayload EncodeConditionApplyEffect(const ConditionApplyEffectData& data);
[[nodiscard]] foundation::Result<ConditionApplyEffectData> DecodeConditionApplyEffect(const effects::RegisteredEffectPayload& payload);

class MaterialResponseEffectRouter
{
  public:
    using Builder = std::function<foundation::Result<std::vector<effects::EffectOperation>>(
        const materials::MaterialResponse&,
        const effects::EffectOperation&)>;

    [[nodiscard]] foundation::Result<void> Register(materials::MaterialResponseTypeId response, Builder builder);
    [[nodiscard]] foundation::Result<std::vector<effects::EffectOperation>> Build(
        const materials::MaterialResponse& response,
        const effects::EffectOperation& parent) const;

  private:
    std::unordered_map<std::uint64_t, Builder> builders_;
};

class EntityTargetStateProvider final : public effects::IEffectTargetStateProvider
{
  public:
    explicit EntityTargetStateProvider(const entities::EntityService& entities) : entities_(entities) {}
    [[nodiscard]] effects::EffectTargetState Resolve(GameplayObjectRef target) const override;

  private:
    const entities::EntityService& entities_;
};

struct BuiltinEffectTypes
{
    effects::EffectTypeId entity_create{};
    effects::EffectTypeId entity_destroy{};
    effects::EffectTypeId entity_convert{};
    effects::EffectTypeId entity_add_tag{};
    effects::EffectTypeId entity_remove_tag{};
    effects::EffectTypeId material_stimulus{};
    effects::EffectTypeId material_add_exposure{};
    effects::EffectTypeId material_remove_exposure{};
    effects::EffectTypeId condition_apply{};
    effects::EffectTypeId condition_remove_type{};
};

class StateEffectAdapter
{
  public:
    StateEffectAdapter(
        entities::EntityService& entities,
        materials::MaterialService& materials,
        conditions::ConditionService& conditions,
        effects::EffectService& effects,
        const GameplayTagRegistry& tags,
        const effects::IEffectTargetStateProvider& target_state,
        MaterialResponseEffectRouter& material_router)
        : entities_(entities), materials_(materials), conditions_(conditions), effects_(effects), tags_(tags),
          target_state_(target_state), material_router_(material_router)
    {
    }

    [[nodiscard]] foundation::Result<BuiltinEffectTypes> RegisterHandlers();

  private:
    entities::EntityService& entities_;
    materials::MaterialService& materials_;
    conditions::ConditionService& conditions_;
    effects::EffectService& effects_;
    const GameplayTagRegistry& tags_;
    const effects::IEffectTargetStateProvider& target_state_;
    MaterialResponseEffectRouter& material_router_;
};

class StateLifecycleAdapter
{
  public:
    StateLifecycleAdapter(
        entities::EntityService& entities,
        materials::MaterialService& materials,
        conditions::ConditionService& conditions,
        effects::EffectService& effects)
        : entities_(entities), materials_(materials), conditions_(conditions), effects_(effects)
    {
    }

    [[nodiscard]] foundation::Result<std::uint64_t> ProcessEntityChanges(GameplayContext context = {});

  private:
    entities::EntityService& entities_;
    materials::MaterialService& materials_;
    conditions::ConditionService& conditions_;
    effects::EffectService& effects_;
    std::uint64_t cursor_ = 0;
};

class ConditionEffectsAdapter
{
  public:
    ConditionEffectsAdapter(conditions::ConditionService& conditions, effects::EffectService& effects)
        : conditions_(conditions), effects_(effects)
    {
    }

    [[nodiscard]] foundation::Result<void> RegisterRoute(ActionTypeId action, effects::EffectDefinitionId definition);
    [[nodiscard]] foundation::Result<std::vector<effects::EffectExecutionResult>> ProcessPending(
        effects::EffectExecutionBudget budget = {},
        core::tasks::ITaskScheduler* scheduler = nullptr);

  private:
    conditions::ConditionService& conditions_;
    effects::EffectService& effects_;
    std::unordered_map<ActionTypeId, effects::EffectDefinitionId> routes_;
    std::uint64_t cursor_ = 0;
};

struct StateTimeProcessResult
{
    std::uint64_t condition_expirations = 0;
    std::uint64_t condition_periodic = 0;
    std::vector<effects::EffectExecutionResult> effect_executions;
    std::vector<time::ScheduledTrigger> unhandled;
};

class StateTimeAdapter
{
  public:
    StateTimeAdapter(
        conditions::ConditionService& conditions,
        effects::EffectService& effects,
        time::GameplayTimeService& time)
        : conditions_(conditions), effects_(effects), time_(time)
    {
    }

    [[nodiscard]] foundation::Result<void> RegisterContracts();
    [[nodiscard]] foundation::Result<std::uint64_t> SynchronizeConditionSchedules(GameplayContext context = {});
    [[nodiscard]] foundation::Result<std::uint64_t> SynchronizeDeferredEffects(GameplayContext context = {});
    [[nodiscard]] foundation::Result<StateTimeProcessResult> ProcessDue(
        ClockId clock,
        GameplayContext context = {},
        time::SchedulerBudget scheduler_budget = {},
        effects::EffectExecutionBudget effect_budget = {},
        core::tasks::ITaskScheduler* scheduler = nullptr);

    [[nodiscard]] ActionTypeId ExpireAction() const noexcept { return expire_action_; }
    [[nodiscard]] ActionTypeId PeriodicAction() const noexcept { return periodic_action_; }
    [[nodiscard]] ActionTypeId DeferredEffectAction() const noexcept { return deferred_effect_action_; }

  private:
    [[nodiscard]] foundation::Result<void> RebuildConditionSchedules(
        const conditions::ConditionInstance& instance,
        GameplayContext context);
    [[nodiscard]] time::CatchUpPolicy MapCatchUp(conditions::PeriodicCatchUpPolicy policy) const noexcept;
    [[nodiscard]] time::SchedulePersistence MapPersistence(conditions::ConditionPersistencePolicy policy) const noexcept;

    conditions::ConditionService& conditions_;
    effects::EffectService& effects_;
    time::GameplayTimeService& time_;
    ActionTypeId expire_action_{};
    ActionTypeId periodic_action_{};
    ActionTypeId deferred_effect_action_{};
    std::uint64_t condition_cursor_ = 0;
    std::uint64_t effect_cursor_ = 0;
};
} // namespace epidemic::gameplay::state_integration
