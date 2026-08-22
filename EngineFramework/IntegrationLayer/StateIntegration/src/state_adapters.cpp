#include "Epidemic/GameFramework/StateIntegration/state_adapters.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace epidemic::gameplay::state_integration
{
namespace
{
constexpr std::string_view kEntityCreatePayloadName = "framework.payload.entity.create.v1";
constexpr std::string_view kEntityConvertPayloadName = "framework.payload.entity.convert.v1";
constexpr std::string_view kEntityTagPayloadName = "framework.payload.entity.tag.v1";
constexpr std::string_view kMaterialStimulusPayloadName = "framework.payload.material.stimulus.v1";
constexpr std::string_view kSubstanceExposurePayloadName = "framework.payload.material.exposure.v1";
constexpr std::string_view kConditionApplyPayloadName = "framework.payload.condition.apply.v1";
constexpr std::string_view kConditionRemoveTypePayloadName = "framework.payload.condition.remove_type.v1";

[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}

void AppendU64(std::vector<std::byte>& bytes, std::uint64_t value)
{
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        bytes.push_back(static_cast<std::byte>((value >> (i * 8u)) & 0xFFu));
    }
}

void AppendU32(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        bytes.push_back(static_cast<std::byte>((value >> (i * 8u)) & 0xFFu));
    }
}

[[nodiscard]] bool ReadU64(std::span<const std::byte> bytes, std::size_t& offset, std::uint64_t& value)
{
    if (offset + 8 > bytes.size())
    {
        return false;
    }
    value = 0;
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (i * 8u);
    }
    offset += 8;
    return true;
}

[[nodiscard]] bool ReadU32(std::span<const std::byte> bytes, std::size_t& offset, std::uint32_t& value)
{
    if (offset + 4 > bytes.size())
    {
        return false;
    }
    value = 0;
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (i * 8u);
    }
    offset += 4;
    return true;
}

template <typename T>
[[nodiscard]] foundation::Result<T> DecodeTrivialPayload(const effects::RegisteredEffectPayload& payload, std::string_view type_name)
{
    const auto value = payload.AsTrivial<T>(TypeId::FromString(type_name));
    if (!value.has_value())
    {
        return foundation::Result<T>::Failure(Error("gameplay.effect_payload_decode", "built-in effect payload is invalid"));
    }
    return foundation::Result<T>::Success(*value);
}

class EntityCreateHandler final : public effects::IEffectHandler
{
  public:
    explicit EntityCreateHandler(entities::EntityService& service) : service_(service) {}
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override { return effects::EffectTypeId::FromString("framework.effect.entity.create"); }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override
    {
        const auto payload = DecodeTrivialPayload<EntityCreateEffectPayload>(operation.payload, kEntityCreatePayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectPrepareResult>::Failure(payload.GetError());
        }
        return foundation::Result<effects::EffectPrepareResult>::Success(
            effects::EffectPrepareResult{service_.FindArchetype(payload.Value().archetype) != nullptr
                                             ? effects::EffectPrepareDisposition::Accepted
                                             : effects::EffectPrepareDisposition::Rejected,
                                         {}});
    }
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(
        const effects::EffectOperation& operation,
        const effects::RegisteredEffectPayload&) override
    {
        const auto payload = DecodeTrivialPayload<EntityCreateEffectPayload>(operation.payload, kEntityCreatePayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(payload.GetError());
        }
        entities::CreateEntityRequest request;
        request.archetype = payload.Value().archetype;
        request.persistence = payload.Value().persistence;
        request.context = operation.context;
        const auto created = service_.Create(std::move(request));
        if (!created)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(created.GetError());
        }
        return foundation::Result<effects::EffectCommitResult>::Success({effects::EffectCommitDisposition::Applied, {}});
    }

  private:
    entities::EntityService& service_;
};

class EntityDestroyHandler final : public effects::IEffectHandler
{
  public:
    explicit EntityDestroyHandler(entities::EntityService& service) : service_(service) {}
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override { return effects::EffectTypeId::FromString("framework.effect.entity.destroy"); }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override
    {
        const auto id = entities::EntityService::FromGameplayObjectRef(operation.target);
        const auto record = service_.Find(id);
        const auto disposition = !record ? effects::EffectPrepareDisposition::InvalidTarget
                                        : (record->lifecycle == entities::EntityLifecycleState::Alive
                                                          ? effects::EffectPrepareDisposition::Accepted
                                                          : effects::EffectPrepareDisposition::Rejected);
        return foundation::Result<effects::EffectPrepareResult>::Success({disposition, {}});
    }
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(
        const effects::EffectOperation& operation,
        const effects::RegisteredEffectPayload&) override
    {
        const auto id = entities::EntityService::FromGameplayObjectRef(operation.target);
        const auto result = service_.RequestDestroy(id, entities::EntityDestroyReason::Destroyed, operation.context);
        if (!result)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(result.GetError());
        }
        return foundation::Result<effects::EffectCommitResult>::Success({effects::EffectCommitDisposition::Applied, {}});
    }

  private:
    entities::EntityService& service_;
};

class EntityConvertHandler final : public effects::IEffectHandler
{
  public:
    explicit EntityConvertHandler(entities::EntityService& service) : service_(service) {}
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override { return effects::EffectTypeId::FromString("framework.effect.entity.convert"); }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override
    {
        const auto payload = DecodeTrivialPayload<EntityConvertEffectPayload>(operation.payload, kEntityConvertPayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectPrepareResult>::Failure(payload.GetError());
        }
        const auto id = entities::EntityService::FromGameplayObjectRef(operation.target);
        const auto disposition = service_.Find(id).has_value() && service_.FindArchetype(payload.Value().archetype) != nullptr
                                     ? effects::EffectPrepareDisposition::Accepted
                                     : effects::EffectPrepareDisposition::InvalidTarget;
        return foundation::Result<effects::EffectPrepareResult>::Success({disposition, {}});
    }
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(
        const effects::EffectOperation& operation,
        const effects::RegisteredEffectPayload&) override
    {
        const auto payload = DecodeTrivialPayload<EntityConvertEffectPayload>(operation.payload, kEntityConvertPayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(payload.GetError());
        }
        const auto result = service_.Convert(entities::EntityService::FromGameplayObjectRef(operation.target), payload.Value().archetype,
                                             entities::EntityConversionPolicy{payload.Value().preserve_instance_tags,
                                                                              payload.Value().preserve_persistence},
                                             operation.context);
        if (!result)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(result.GetError());
        }
        return foundation::Result<effects::EffectCommitResult>::Success({effects::EffectCommitDisposition::Applied, {}});
    }

  private:
    entities::EntityService& service_;
};

class EntityTagHandler final : public effects::IEffectHandler
{
  public:
    EntityTagHandler(entities::EntityService& service, bool add) : service_(service), add_(add) {}
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override
    {
        return effects::EffectTypeId::FromString(add_ ? "framework.effect.entity.add_tag" : "framework.effect.entity.remove_tag");
    }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override
    {
        const auto payload = DecodeTrivialPayload<EntityTagEffectPayload>(operation.payload, kEntityTagPayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectPrepareResult>::Failure(payload.GetError());
        }
        const auto id = entities::EntityService::FromGameplayObjectRef(operation.target);
        return foundation::Result<effects::EffectPrepareResult>::Success(
            {service_.Find(id).has_value() && payload.Value().tag.IsValid() ? effects::EffectPrepareDisposition::Accepted
                                                                           : effects::EffectPrepareDisposition::InvalidTarget,
             {}});
    }
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(
        const effects::EffectOperation& operation,
        const effects::RegisteredEffectPayload&) override
    {
        const auto payload = DecodeTrivialPayload<EntityTagEffectPayload>(operation.payload, kEntityTagPayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(payload.GetError());
        }
        const auto id = entities::EntityService::FromGameplayObjectRef(operation.target);
        const auto result = add_ ? service_.AddTag(id, payload.Value().tag, operation.context)
                                 : service_.RemoveTag(id, payload.Value().tag, operation.context);
        if (!result)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(result.GetError());
        }
        return foundation::Result<effects::EffectCommitResult>::Success({effects::EffectCommitDisposition::Applied, {}});
    }

  private:
    entities::EntityService& service_;
    bool add_ = true;
};

class MaterialStimulusHandler final : public effects::IEffectHandler
{
  public:
    MaterialStimulusHandler(
        materials::MaterialService& service,
        const GameplayTagRegistry& tags,
        const MaterialResponseEffectRouter& router)
        : service_(service), tags_(tags), router_(router)
    {
    }
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override { return effects::EffectTypeId::FromString("framework.effect.material.stimulus"); }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override
    {
        const auto payload = DecodeTrivialPayload<MaterialStimulusEffectPayload>(operation.payload, kMaterialStimulusPayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectPrepareResult>::Failure(payload.GetError());
        }
        return foundation::Result<effects::EffectPrepareResult>::Success(
            {service_.FindState(operation.target, payload.Value().slot).has_value() ? effects::EffectPrepareDisposition::Accepted
                                                                                  : effects::EffectPrepareDisposition::InvalidTarget,
             {}});
    }
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(
        const effects::EffectOperation& operation,
        const effects::RegisteredEffectPayload&) override
    {
        const auto payload = DecodeTrivialPayload<MaterialStimulusEffectPayload>(operation.payload, kMaterialStimulusPayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(payload.GetError());
        }
        const materials::MaterialStimulus stimulus{operation.target, payload.Value().slot, payload.Value().stimulus,
                                                   operation.magnitude_micro, operation.context};
        auto response_result = service_.ApplyStimulus(stimulus, tags_);
        if (!response_result)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(response_result.GetError());
        }
        std::vector<effects::EffectOperation> derived;
        for (const auto& response : response_result.Value())
        {
            auto built = router_.Build(response, operation);
            if (!built)
            {
                return foundation::Result<effects::EffectCommitResult>::Failure(built.GetError());
            }
            auto values = std::move(built).Value();
            derived.insert(derived.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
        }
        return foundation::Result<effects::EffectCommitResult>::Success({effects::EffectCommitDisposition::Applied, std::move(derived)});
    }

  private:
    materials::MaterialService& service_;
    const GameplayTagRegistry& tags_;
    const MaterialResponseEffectRouter& router_;
};

class SubstanceExposureHandler final : public effects::IEffectHandler
{
  public:
    SubstanceExposureHandler(materials::MaterialService& service, bool add) : service_(service), add_(add) {}
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override
    {
        return effects::EffectTypeId::FromString(add_ ? "framework.effect.material.add_exposure"
                                                       : "framework.effect.material.remove_exposure");
    }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override
    {
        const auto payload = DecodeTrivialPayload<SubstanceExposureEffectPayload>(operation.payload, kSubstanceExposurePayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectPrepareResult>::Failure(payload.GetError());
        }
        const bool valid = service_.FindState(operation.target, payload.Value().slot).has_value() &&
                           service_.FindSubstance(payload.Value().substance) != nullptr;
        return foundation::Result<effects::EffectPrepareResult>::Success(
            {valid ? effects::EffectPrepareDisposition::Accepted : effects::EffectPrepareDisposition::InvalidTarget, {}});
    }
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(
        const effects::EffectOperation& operation,
        const effects::RegisteredEffectPayload&) override
    {
        const auto payload = DecodeTrivialPayload<SubstanceExposureEffectPayload>(operation.payload, kSubstanceExposurePayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(payload.GetError());
        }
        foundation::Result<void> result = add_ ? service_.ApplySubstanceExposure(operation.target, payload.Value().slot,
                                                                                  payload.Value().substance, operation.magnitude_micro,
                                                                                  payload.Value().coverage_ppm, operation.context)
                                                : service_.RemoveSubstanceExposure(operation.target, payload.Value().slot,
                                                                                   payload.Value().substance, operation.context);
        if (!result)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(result.GetError());
        }
        return foundation::Result<effects::EffectCommitResult>::Success({effects::EffectCommitDisposition::Applied, {}});
    }

  private:
    materials::MaterialService& service_;
    bool add_ = true;
};

class ConditionApplyHandler final : public effects::IEffectHandler
{
  public:
    ConditionApplyHandler(
        conditions::ConditionService& service,
        const effects::IEffectTargetStateProvider& target_state)
        : service_(service), target_state_(target_state)
    {
    }
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override { return effects::EffectTypeId::FromString("framework.effect.condition.apply"); }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override
    {
        const auto decoded = DecodeConditionApplyEffect(operation.payload);
        if (!decoded)
        {
            return foundation::Result<effects::EffectPrepareResult>::Failure(decoded.GetError());
        }
        const auto* definition = service_.FindDefinition(decoded.Value().type);
        if (definition == nullptr)
        {
            return foundation::Result<effects::EffectPrepareResult>::Success({effects::EffectPrepareDisposition::Unsupported, {}});
        }
        if (definition->materialization == conditions::ConditionMaterializationPolicy::MaterializedOnly &&
            !target_state_.Resolve(operation.target).materialized)
        {
            return foundation::Result<effects::EffectPrepareResult>::Success({effects::EffectPrepareDisposition::Unavailable, {}});
        }
        return foundation::Result<effects::EffectPrepareResult>::Success({effects::EffectPrepareDisposition::Accepted, {}});
    }
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(
        const effects::EffectOperation& operation,
        const effects::RegisteredEffectPayload&) override
    {
        auto decoded = DecodeConditionApplyEffect(operation.payload);
        if (!decoded)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(decoded.GetError());
        }
        conditions::ApplyConditionRequest request;
        request.type = decoded.Value().type;
        request.subject = operation.target;
        request.source = operation.context.source;
        request.instigator = operation.context.instigator;
        request.magnitude_micro = operation.magnitude_micro;
        request.duration = decoded.Value().duration;
        request.payload = std::move(decoded.Value().condition_payload);
        request.context = operation.context;
        const auto applied = service_.Apply(std::move(request));
        if (!applied)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(applied.GetError());
        }
        const auto disposition = applied.Value().disposition == conditions::ConditionApplyDisposition::Rejected ||
                                         applied.Value().disposition == conditions::ConditionApplyDisposition::NoOp
                                     ? effects::EffectCommitDisposition::NoOp
                                     : effects::EffectCommitDisposition::Applied;
        return foundation::Result<effects::EffectCommitResult>::Success({disposition, {}});
    }

  private:
    conditions::ConditionService& service_;
    const effects::IEffectTargetStateProvider& target_state_;
};

class ConditionRemoveTypeHandler final : public effects::IEffectHandler
{
  public:
    explicit ConditionRemoveTypeHandler(conditions::ConditionService& service) : service_(service) {}
    [[nodiscard]] effects::EffectTypeId Type() const noexcept override { return effects::EffectTypeId::FromString("framework.effect.condition.remove_type"); }
    [[nodiscard]] effects::EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<effects::EffectPrepareResult> Prepare(const effects::EffectOperation& operation) const override
    {
        const auto payload = DecodeTrivialPayload<ConditionRemoveTypeEffectPayload>(operation.payload, kConditionRemoveTypePayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectPrepareResult>::Failure(payload.GetError());
        }
        return foundation::Result<effects::EffectPrepareResult>::Success(
            {service_.HasCondition(operation.target, payload.Value().type) ? effects::EffectPrepareDisposition::Accepted
                                                                          : effects::EffectPrepareDisposition::NoOp,
             {}});
    }
    [[nodiscard]] foundation::Result<effects::EffectCommitResult> Commit(
        const effects::EffectOperation& operation,
        const effects::RegisteredEffectPayload&) override
    {
        const auto payload = DecodeTrivialPayload<ConditionRemoveTypeEffectPayload>(operation.payload, kConditionRemoveTypePayloadName);
        if (!payload)
        {
            return foundation::Result<effects::EffectCommitResult>::Failure(payload.GetError());
        }
        const auto count = service_.RemoveByType(operation.target, payload.Value().type,
                                                 conditions::ConditionRemovalReason::RemovedByEffect, operation.context);
        return foundation::Result<effects::EffectCommitResult>::Success(
            {count == 0 ? effects::EffectCommitDisposition::NoOp : effects::EffectCommitDisposition::Applied, {}});
    }

  private:
    conditions::ConditionService& service_;
};
} // namespace

foundation::Result<void> StateQueryAdapter::RegisterProviders()
{
    queries::QueryProviderCapabilities capabilities;
    capabilities.supports_current = true;
    capabilities.supports_snapshot = false;

    auto get_entity = queries_.RegisterProvider<GetEntityQuery>(
        "framework.query.entities.get", capabilities,
        [this](const GetEntityQuery& query, const queries::QueryContext&) {
            auto value = entities_.Find(query.id);
            queries::QueryMetadata metadata;
            metadata.revision = entities_.CurrentRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = value.has_value() ? 1 : 0;
            metadata.work_units = 1;
            return foundation::Result<queries::QueryResponse<GetEntityQuery::ResultType>>::Success({std::move(value), metadata});
        });
    if (!get_entity)
    {
        return get_entity;
    }

    auto find_entities = queries_.RegisterProvider<FindEntitiesQuery>(
        "framework.query.entities.find", capabilities,
        [this](const FindEntitiesQuery& query, const queries::QueryContext&) {
            auto values = entities_.AllEntities();
            const auto work = values.size();
            values.erase(std::remove_if(values.begin(), values.end(), [this, &query](const entities::EntityRecord& value) {
                if (query.archetype.has_value() && value.archetype != *query.archetype)
                {
                    return true;
                }
                if (query.lifecycle.has_value() && value.lifecycle != *query.lifecycle)
                {
                    return true;
                }
                if (query.materialization.has_value() && value.materialization != *query.materialization)
                {
                    return true;
                }
                if (query.tag.has_value())
                {
                    const auto* definition = entities_.FindArchetype(value.archetype);
                    const bool archetype_has = definition != nullptr && definition->tags.HasMatching(*query.tag, tags_);
                    if (!archetype_has && !value.instance_tags.HasMatching(*query.tag, tags_))
                    {
                        return true;
                    }
                }
                return false;
            }), values.end());
            queries::QueryMetadata metadata;
            metadata.revision = entities_.CurrentRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = work;
            return foundation::Result<queries::QueryResponse<FindEntitiesQuery::ResultType>>::Success({std::move(values), metadata});
        });
    if (!find_entities)
    {
        return find_entities;
    }

    auto materials_provider = queries_.RegisterProvider<GetMaterialSlotsQuery>(
        "framework.query.materials.slots", capabilities,
        [this](const GetMaterialSlotsQuery& query, const queries::QueryContext&) {
            auto values = materials_.FindSlots(query.subject);
            queries::QueryMetadata metadata;
            metadata.revision = materials_.CurrentRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            return foundation::Result<queries::QueryResponse<GetMaterialSlotsQuery::ResultType>>::Success({std::move(values), metadata});
        });
    if (!materials_provider)
    {
        return materials_provider;
    }

    return queries_.RegisterProvider<GetConditionsQuery>(
        "framework.query.conditions.subject", capabilities,
        [this](const GetConditionsQuery& query, const queries::QueryContext&) {
            auto values = conditions_.GetConditions(query.subject);
            queries::QueryMetadata metadata;
            metadata.revision = conditions_.CurrentRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            return foundation::Result<queries::QueryResponse<GetConditionsQuery::ResultType>>::Success({std::move(values), metadata});
        });
}

foundation::Result<void> StateFactsAdapter::RegisterContracts()
{
    const auto entity = facts_.RegisterEventType<entities::EntityChange>(
        "framework.entities.changed", entities::EntityService::Domain(), facts::HistoryPolicy::Recent, 4096);
    if (!entity)
    {
        return foundation::Result<void>::Failure(entity.GetError());
    }
    entity_changed_ = entity.Value();

    const auto material = facts_.RegisterEventType<materials::MaterialChange>(
        "framework.materials.changed", materials::MaterialService::Domain(), facts::HistoryPolicy::Recent, 4096);
    if (!material)
    {
        return foundation::Result<void>::Failure(material.GetError());
    }
    material_changed_ = material.Value();

    const auto condition = facts_.RegisterEventType<conditions::ConditionChange>(
        "framework.conditions.changed", conditions::ConditionService::Domain(), facts::HistoryPolicy::Recent, 4096);
    if (!condition)
    {
        return foundation::Result<void>::Failure(condition.GetError());
    }
    condition_changed_ = condition.Value();

    const auto effect = facts_.RegisterEventType<effects::EffectChange>(
        "framework.effects.changed", effects::EffectService::Domain(), facts::HistoryPolicy::Recent, 4096);
    if (!effect)
    {
        return foundation::Result<void>::Failure(effect.GetError());
    }
    effect_changed_ = effect.Value();

    const auto condition_fact = facts_.RegisterFactType<ConditionFactValue>(
        "framework.condition.active", conditions::ConditionService::Domain());
    if (!condition_fact)
    {
        return foundation::Result<void>::Failure(condition_fact.GetError());
    }
    active_condition_fact_ = condition_fact.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<std::uint64_t> StateFactsAdapter::PublishPendingChanges(GameplayContext context)
{
    std::uint64_t published = 0;
    const auto producer = ProducerId::FromString("framework.state_integration");

    for (const auto& change : entities_.ChangesSince(entity_cursor_))
    {
        auto event_context = change.context.tick.IsValid() ? change.context : context;
        const auto result = facts_.Publish(entity_changed_, event_context, entities::EntityService::ToGameplayObjectRef(change.entity), change, producer);
        if (!result)
        {
            return foundation::Result<std::uint64_t>::Failure(result.GetError());
        }
        entity_cursor_ = change.sequence;
        ++published;
    }
    for (const auto& change : materials_.ChangesSince(material_cursor_))
    {
        auto event_context = change.context.tick.IsValid() ? change.context : context;
        const auto result = facts_.Publish(material_changed_, event_context, change.key.subject, change, producer);
        if (!result)
        {
            return foundation::Result<std::uint64_t>::Failure(result.GetError());
        }
        material_cursor_ = change.sequence;
        ++published;
    }

    struct PendingConditionFactMutation
    {
        facts::FactKey key{};
        conditions::ConditionChange change{};
    };
    std::vector<PendingConditionFactMutation> pending_fact_mutations;
    std::unordered_map<facts::FactKey, std::size_t, facts::FactKeyHash> pending_fact_indices;

    for (const auto& change : conditions_.ChangesSince(condition_cursor_))
    {
        auto event_context = change.context.tick.IsValid() ? change.context : context;
        const auto event_result = facts_.Publish(condition_changed_, event_context, change.subject, change, producer);
        if (!event_result)
        {
            return foundation::Result<std::uint64_t>::Failure(event_result.GetError());
        }
        const auto* definition = conditions_.FindDefinition(change.type);
        if (definition != nullptr && definition->publish_fact)
        {
            const auto key = facts::FactKey{active_condition_fact_, change.subject, ConditionScope(change.instance)};
            const auto [found, inserted] = pending_fact_indices.emplace(key, pending_fact_mutations.size());
            if (inserted)
            {
                pending_fact_mutations.push_back(PendingConditionFactMutation{key, change});
            }
            else
            {
                pending_fact_mutations[found->second].change = change;
            }
        }
        condition_cursor_ = change.sequence;
        ++published;
    }

    if (!pending_fact_mutations.empty())
    {
        auto transaction = facts_.BeginTransaction(conditions::ConditionService::Domain());
        for (const auto& pending : pending_fact_mutations)
        {
            const auto& change = pending.change;
            const auto* definition = conditions_.FindDefinition(change.type);
            if (change.kind == conditions::ConditionChangeKind::Removed || change.kind == conditions::ConditionChangeKind::Expired)
            {
                transaction.Remove(pending.key);
            }
            else if (definition != nullptr)
            {
                const auto* instance = conditions_.Find(change.instance);
                if (instance == nullptr)
                {
                    transaction.Remove(pending.key);
                    continue;
                }
                const auto persistence = instance->expires_at.has_value()
                                             ? facts::FactPersistence::Timed
                                             : (definition->persistence == conditions::ConditionPersistencePolicy::Persistent
                                                    ? facts::FactPersistence::Persistent
                                                    : facts::FactPersistence::Session);
                transaction.Set(active_condition_fact_, change.subject, ConditionScope(change.instance),
                                ConditionFactValue{instance->type, instance->magnitude_micro, instance->stacks,
                                                   instance->paused_for_materialization},
                                persistence, instance->expires_at);
            }
        }
        const auto commit = facts_.Commit(std::move(transaction), context);
        if (!commit)
        {
            return foundation::Result<std::uint64_t>::Failure(commit.GetError());
        }
    }

    for (const auto& change : effects_.ChangesSince(effect_cursor_))
    {
        auto event_context = change.context.tick.IsValid() ? change.context : context;
        const auto result = facts_.Publish(effect_changed_, event_context, change.target, change, producer);
        if (!result)
        {
            return foundation::Result<std::uint64_t>::Failure(result.GetError());
        }
        effect_cursor_ = change.sequence;
        ++published;
    }

    return foundation::Result<std::uint64_t>::Success(published);
}

effects::RegisteredEffectPayload EncodeConditionApplyEffect(const ConditionApplyEffectData& data)
{
    effects::RegisteredEffectPayload result;
    result.type = TypeId::FromString(kConditionApplyPayloadName);
    result.bytes.reserve(8 + 1 + 8 + 8 + 4 + data.condition_payload.bytes.size());
    AppendU64(result.bytes, data.type.Raw());
    result.bytes.push_back(data.duration.has_value() ? std::byte{1} : std::byte{0});
    AppendU64(result.bytes, data.duration.has_value() ? static_cast<std::uint64_t>(data.duration->ticks) : 0);
    AppendU64(result.bytes, data.condition_payload.type.Raw());
    AppendU32(result.bytes, static_cast<std::uint32_t>(data.condition_payload.bytes.size()));
    result.bytes.insert(result.bytes.end(), data.condition_payload.bytes.begin(), data.condition_payload.bytes.end());
    return result;
}

foundation::Result<ConditionApplyEffectData> DecodeConditionApplyEffect(const effects::RegisteredEffectPayload& payload)
{
    if (payload.type != TypeId::FromString(kConditionApplyPayloadName))
    {
        return foundation::Result<ConditionApplyEffectData>::Failure(Error("gameplay.condition_effect_payload_type", "condition apply effect payload type is invalid"));
    }
    std::size_t offset = 0;
    std::uint64_t type_raw = 0;
    std::uint64_t duration_raw = 0;
    std::uint64_t inner_type_raw = 0;
    std::uint32_t inner_size = 0;
    if (!ReadU64(payload.bytes, offset, type_raw) || offset >= payload.bytes.size())
    {
        return foundation::Result<ConditionApplyEffectData>::Failure(Error("gameplay.condition_effect_payload_decode", "condition apply payload is truncated"));
    }
    const bool has_duration = std::to_integer<std::uint8_t>(payload.bytes[offset++]) != 0;
    if (!ReadU64(payload.bytes, offset, duration_raw) || !ReadU64(payload.bytes, offset, inner_type_raw) ||
        !ReadU32(payload.bytes, offset, inner_size) || offset + inner_size != payload.bytes.size())
    {
        return foundation::Result<ConditionApplyEffectData>::Failure(Error("gameplay.condition_effect_payload_decode", "condition apply payload is malformed"));
    }

    ConditionApplyEffectData data;
    data.type = conditions::ConditionTypeId{TypeId{type_raw}};
    if (has_duration)
    {
        data.duration = GameplayDuration{static_cast<std::int64_t>(duration_raw)};
    }
    if (inner_type_raw != 0 || inner_size != 0)
    {
        data.condition_payload.type = TypeId{inner_type_raw};
        data.condition_payload.bytes.assign(payload.bytes.begin() + static_cast<std::ptrdiff_t>(offset), payload.bytes.end());
    }
    return foundation::Result<ConditionApplyEffectData>::Success(std::move(data));
}

foundation::Result<void> MaterialResponseEffectRouter::Register(materials::MaterialResponseTypeId response, Builder builder)
{
    if (!response.IsValid() || !builder)
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_response_route_invalid", "material response route requires valid response and builder"));
    }
    if (builders_.contains(response.Raw()))
    {
        return foundation::Result<void>::Failure(Error("gameplay.already_registered", "material response route is already registered"));
    }
    builders_.emplace(response.Raw(), std::move(builder));
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<effects::EffectOperation>> MaterialResponseEffectRouter::Build(
    const materials::MaterialResponse& response,
    const effects::EffectOperation& parent) const
{
    const auto found = builders_.find(response.type.Raw());
    if (found == builders_.end())
    {
        return foundation::Result<std::vector<effects::EffectOperation>>::Success({});
    }
    return found->second(response, parent);
}

effects::EffectTargetState EntityTargetStateProvider::Resolve(GameplayObjectRef target) const
{
    if (target.domain != entities::EntityService::Domain())
    {
        return effects::EffectTargetState{true, false, false};
    }
    const auto record = entities_.Find(entities::EntityService::FromGameplayObjectRef(target));
    if (!record)
    {
        return effects::EffectTargetState{false, false, false};
    }
    const bool materialized = record->materialization == entities::EntityMaterializationState::Materialized;
    return effects::EffectTargetState{true, materialized, materialized};
}

foundation::Result<BuiltinEffectTypes> StateEffectAdapter::RegisterHandlers()
{
    effects_.SetTargetStateProvider(&target_state_);
    BuiltinEffectTypes result;

    auto register_handler = [this](std::string_view name, std::shared_ptr<effects::IEffectHandler> handler,
                                   TypeId payload_type = {}, std::size_t max_bytes = 0,
                                   effects::EffectService::PayloadValidator validator = {}) -> foundation::Result<effects::EffectTypeId> {
        return effects_.RegisterHandler(name, std::move(handler), payload_type, max_bytes, std::move(validator));
    };

    const auto entity_create = register_handler("framework.effect.entity.create", std::make_shared<EntityCreateHandler>(entities_),
                                                TypeId::FromString(kEntityCreatePayloadName), sizeof(EntityCreateEffectPayload));
    if (!entity_create) return foundation::Result<BuiltinEffectTypes>::Failure(entity_create.GetError());
    result.entity_create = entity_create.Value();

    const auto entity_destroy = register_handler("framework.effect.entity.destroy", std::make_shared<EntityDestroyHandler>(entities_));
    if (!entity_destroy) return foundation::Result<BuiltinEffectTypes>::Failure(entity_destroy.GetError());
    result.entity_destroy = entity_destroy.Value();

    const auto entity_convert = register_handler("framework.effect.entity.convert", std::make_shared<EntityConvertHandler>(entities_),
                                                 TypeId::FromString(kEntityConvertPayloadName), sizeof(EntityConvertEffectPayload));
    if (!entity_convert) return foundation::Result<BuiltinEffectTypes>::Failure(entity_convert.GetError());
    result.entity_convert = entity_convert.Value();

    const auto add_tag = register_handler("framework.effect.entity.add_tag", std::make_shared<EntityTagHandler>(entities_, true),
                                          TypeId::FromString(kEntityTagPayloadName), sizeof(EntityTagEffectPayload));
    if (!add_tag) return foundation::Result<BuiltinEffectTypes>::Failure(add_tag.GetError());
    result.entity_add_tag = add_tag.Value();

    const auto remove_tag = register_handler("framework.effect.entity.remove_tag", std::make_shared<EntityTagHandler>(entities_, false),
                                             TypeId::FromString(kEntityTagPayloadName), sizeof(EntityTagEffectPayload));
    if (!remove_tag) return foundation::Result<BuiltinEffectTypes>::Failure(remove_tag.GetError());
    result.entity_remove_tag = remove_tag.Value();

    const auto material_stimulus = register_handler("framework.effect.material.stimulus",
                                                    std::make_shared<MaterialStimulusHandler>(materials_, tags_, material_router_),
                                                    TypeId::FromString(kMaterialStimulusPayloadName), sizeof(MaterialStimulusEffectPayload));
    if (!material_stimulus) return foundation::Result<BuiltinEffectTypes>::Failure(material_stimulus.GetError());
    result.material_stimulus = material_stimulus.Value();

    const auto add_exposure = register_handler("framework.effect.material.add_exposure", std::make_shared<SubstanceExposureHandler>(materials_, true),
                                               TypeId::FromString(kSubstanceExposurePayloadName), sizeof(SubstanceExposureEffectPayload));
    if (!add_exposure) return foundation::Result<BuiltinEffectTypes>::Failure(add_exposure.GetError());
    result.material_add_exposure = add_exposure.Value();

    const auto remove_exposure = register_handler("framework.effect.material.remove_exposure", std::make_shared<SubstanceExposureHandler>(materials_, false),
                                                  TypeId::FromString(kSubstanceExposurePayloadName), sizeof(SubstanceExposureEffectPayload));
    if (!remove_exposure) return foundation::Result<BuiltinEffectTypes>::Failure(remove_exposure.GetError());
    result.material_remove_exposure = remove_exposure.Value();

    const auto condition_apply = register_handler(
        "framework.effect.condition.apply", std::make_shared<ConditionApplyHandler>(conditions_, target_state_),
        TypeId::FromString(kConditionApplyPayloadName), 64 * 1024,
        [](std::span<const std::byte> bytes) {
            effects::RegisteredEffectPayload payload;
            payload.type = TypeId::FromString(kConditionApplyPayloadName);
            payload.bytes.assign(bytes.begin(), bytes.end());
            return static_cast<bool>(DecodeConditionApplyEffect(payload));
        });
    if (!condition_apply) return foundation::Result<BuiltinEffectTypes>::Failure(condition_apply.GetError());
    result.condition_apply = condition_apply.Value();

    const auto condition_remove = register_handler("framework.effect.condition.remove_type", std::make_shared<ConditionRemoveTypeHandler>(conditions_),
                                                   TypeId::FromString(kConditionRemoveTypePayloadName), sizeof(ConditionRemoveTypeEffectPayload));
    if (!condition_remove) return foundation::Result<BuiltinEffectTypes>::Failure(condition_remove.GetError());
    result.condition_remove_type = condition_remove.Value();

    return foundation::Result<BuiltinEffectTypes>::Success(result);
}

foundation::Result<std::uint64_t> StateLifecycleAdapter::ProcessEntityChanges(GameplayContext context)
{
    std::uint64_t processed = 0;
    for (const auto& change : entities_.ChangesSince(cursor_))
    {
        const auto ref = entities::EntityService::ToGameplayObjectRef(change.entity);
        const auto effective_context = change.context.tick.IsValid() ? change.context : context;
        if (change.kind == entities::EntityChangeKind::Destroyed)
        {
            const auto removed_materials = materials_.RemoveSubject(ref, effective_context);
            if (!removed_materials)
            {
                return foundation::Result<std::uint64_t>::Failure(removed_materials.GetError());
            }
            [[maybe_unused]] const auto removed_conditions = conditions_.RemoveSubject(
                ref, conditions::ConditionRemovalReason::SubjectDestroyed, effective_context);
            [[maybe_unused]] const auto cancelled_effects = effects_.CancelDeferredTargeting(ref, effective_context);
        }
        else if (change.kind == entities::EntityChangeKind::MaterializationChanged)
        {
            const bool materialized = change.materialization == entities::EntityMaterializationState::Materialized;
            const auto result = conditions_.NotifySubjectMaterialization(ref, materialized, effective_context);
            if (!result)
            {
                return foundation::Result<std::uint64_t>::Failure(result.GetError());
            }
        }
        cursor_ = change.sequence;
        ++processed;
    }
    return foundation::Result<std::uint64_t>::Success(processed);
}

foundation::Result<void> ConditionEffectsAdapter::RegisterRoute(ActionTypeId action, effects::EffectDefinitionId definition)
{
    if (!action.IsValid() || effects_.FindDefinition(definition) == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_effect_route_invalid", "condition action route requires valid action and effect definition"));
    }
    if (routes_.contains(action))
    {
        return foundation::Result<void>::Failure(Error("gameplay.already_registered", "condition action route is already registered"));
    }
    routes_.emplace(action, definition);
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<effects::EffectExecutionResult>> ConditionEffectsAdapter::ProcessPending(
    effects::EffectExecutionBudget budget,
    core::tasks::ITaskScheduler* scheduler)
{
    std::vector<effects::EffectExecutionResult> results;
    const auto changes = conditions_.ChangesSince(cursor_);
    for (const auto& change : changes)
    {
        const auto* definition = conditions_.FindDefinition(change.type);
        if (definition == nullptr)
        {
            cursor_ = change.sequence;
            continue;
        }

        ActionTypeId action{};
        std::uint64_t occurrence_count = 1;
        if (change.kind == conditions::ConditionChangeKind::Added)
        {
            action = definition->on_apply;
        }
        else if (change.kind == conditions::ConditionChangeKind::PeriodicDue)
        {
            action = definition->on_periodic;
            occurrence_count = change.occurrence_count;
        }
        else if (change.kind == conditions::ConditionChangeKind::Removed || change.kind == conditions::ConditionChangeKind::Expired)
        {
            if (change.removal_reason != conditions::ConditionRemovalReason::SubjectDestroyed &&
                change.removal_reason != conditions::ConditionRemovalReason::SystemCleanup)
            {
                action = definition->on_remove;
            }
        }

        const auto route = routes_.find(action);
        if (action.IsValid() && route != routes_.end())
        {
            effects::EffectRequest request;
            request.definition = route->second;
            request.targets = {change.subject};
            request.context = change.context;
            if (const auto* instance = conditions_.Find(change.instance))
            {
                request.source = instance->source;
                request.instigator = instance->instigator;
            }
            else
            {
                request.source = change.source;
                request.instigator = change.instigator;
            }
            const auto max_scale_occurrences = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 1'000'000);
            request.scale_micro = occurrence_count > max_scale_occurrences
                                      ? std::numeric_limits<std::int64_t>::max()
                                      : static_cast<std::int64_t>(occurrence_count * 1'000'000ull);
            auto executed = effects_.Execute(std::move(request), budget, scheduler);
            if (!executed)
            {
                return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(executed.GetError());
            }
            results.push_back(std::move(executed).Value());
        }
        cursor_ = change.sequence;
    }
    return foundation::Result<std::vector<effects::EffectExecutionResult>>::Success(std::move(results));
}

time::CatchUpPolicy StateTimeAdapter::MapCatchUp(conditions::PeriodicCatchUpPolicy policy) const noexcept
{
    switch (policy)
    {
    case conditions::PeriodicCatchUpPolicy::FireEach: return time::CatchUpPolicy::FireEach;
    case conditions::PeriodicCatchUpPolicy::FireOnce: return time::CatchUpPolicy::FireOnce;
    case conditions::PeriodicCatchUpPolicy::Aggregate: return time::CatchUpPolicy::Aggregate;
    case conditions::PeriodicCatchUpPolicy::Skip: return time::CatchUpPolicy::SkipMissed;
    }
    return time::CatchUpPolicy::Aggregate;
}

time::SchedulePersistence StateTimeAdapter::MapPersistence(conditions::ConditionPersistencePolicy policy) const noexcept
{
    return policy == conditions::ConditionPersistencePolicy::Persistent ? time::SchedulePersistence::Persistent
                                                                        : time::SchedulePersistence::Session;
}

foundation::Result<void> StateTimeAdapter::RegisterContracts()
{
    const auto expire = time_.RegisterAction("framework.conditions.expire", conditions::ConditionService::Domain());
    if (!expire)
    {
        return foundation::Result<void>::Failure(expire.GetError());
    }
    expire_action_ = expire.Value();
    const auto periodic = time_.RegisterAction("framework.conditions.periodic", conditions::ConditionService::Domain());
    if (!periodic)
    {
        return foundation::Result<void>::Failure(periodic.GetError());
    }
    periodic_action_ = periodic.Value();
    const auto deferred = time_.RegisterAction("framework.effects.deferred", effects::EffectService::Domain());
    if (!deferred)
    {
        return foundation::Result<void>::Failure(deferred.GetError());
    }
    deferred_effect_action_ = deferred.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> StateTimeAdapter::RebuildConditionSchedules(
    const conditions::ConditionInstance& instance,
    GameplayContext context)
{
    const auto* definition = conditions_.FindDefinition(instance.type);
    if (definition == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_unknown", "condition definition is missing while scheduling"));
    }
    if (instance.expiration_schedule.has_value() && time_.HasSchedule(*instance.expiration_schedule))
    {
        [[maybe_unused]] const auto cancelled = time_.Cancel(*instance.expiration_schedule);
    }
    if (instance.periodic_schedule.has_value() && time_.HasSchedule(*instance.periodic_schedule))
    {
        [[maybe_unused]] const auto cancelled = time_.Cancel(*instance.periodic_schedule);
    }

    std::optional<ScheduleId> expiration;
    std::optional<ScheduleId> periodic;
    const auto owner = GameplayObjectRef{conditions::ConditionService::Domain(), instance.id.value};
    if (instance.expires_at.has_value())
    {
        const auto scheduled = time_.Schedule(definition->clock, *instance.expires_at, owner, expire_action_, {},
                                              time::CatchUpPolicy::FireOnce, MapPersistence(definition->persistence));
        if (!scheduled)
        {
            return foundation::Result<void>::Failure(scheduled.GetError());
        }
        expiration = scheduled.Value();
    }
    if (definition->periodic_interval.ticks > 0)
    {
        const auto due = ::epidemic::gameplay::CheckedAdd(instance.applied_at, definition->periodic_interval);
        if (!due.has_value())
        {
            if (expiration.has_value() && time_.HasSchedule(*expiration))
            {
                [[maybe_unused]] const auto cancelled = time_.Cancel(*expiration);
            }
            return foundation::Result<void>::Failure(Error("gameplay.time_overflow", "condition periodic schedule overflows gameplay time"));
        }
        time::RecurrenceRule recurrence;
        recurrence.kind = time::RecurrenceKind::FixedInterval;
        recurrence.interval = definition->periodic_interval;
        const auto scheduled = time_.Schedule(definition->clock, *due, owner, periodic_action_, recurrence,
                                              MapCatchUp(definition->periodic_catch_up), MapPersistence(definition->persistence));
        if (!scheduled)
        {
            if (expiration.has_value() && time_.HasSchedule(*expiration))
            {
                [[maybe_unused]] const auto cancelled = time_.Cancel(*expiration);
            }
            return foundation::Result<void>::Failure(scheduled.GetError());
        }
        periodic = scheduled.Value();
    }
    return conditions_.SetScheduleLinks(instance.id, expiration, periodic, context);
}

foundation::Result<std::uint64_t> StateTimeAdapter::SynchronizeConditionSchedules(GameplayContext context)
{
    std::uint64_t processed = 0;
    const auto changes = conditions_.ChangesSince(condition_cursor_);
    for (const auto& change : changes)
    {
        if (change.kind == conditions::ConditionChangeKind::Added || change.kind == conditions::ConditionChangeKind::Refreshed)
        {
            if (const auto* instance = conditions_.Find(change.instance))
            {
                const auto result = RebuildConditionSchedules(*instance, change.context.tick.IsValid() ? change.context : context);
                if (!result)
                {
                    return foundation::Result<std::uint64_t>::Failure(result.GetError());
                }
            }
        }
        else if (change.kind == conditions::ConditionChangeKind::DurationExtended)
        {
            if (change.expiration_schedule.has_value() && time_.HasSchedule(*change.expiration_schedule))
            {
                [[maybe_unused]] const auto cancelled = time_.Cancel(*change.expiration_schedule);
            }
            if (const auto* instance = conditions_.Find(change.instance))
            {
                const auto* definition = conditions_.FindDefinition(instance->type);
                if (definition != nullptr && instance->expires_at.has_value())
                {
                    const GameplayObjectRef owner{conditions::ConditionService::Domain(), instance->id.value};
                    const auto scheduled = time_.Schedule(definition->clock, *instance->expires_at, owner, expire_action_, {},
                                                          time::CatchUpPolicy::FireOnce, MapPersistence(definition->persistence));
                    if (!scheduled)
                    {
                        return foundation::Result<std::uint64_t>::Failure(scheduled.GetError());
                    }
                    const auto linked = conditions_.SetScheduleLinks(instance->id, scheduled.Value(), instance->periodic_schedule,
                                                                     change.context.tick.IsValid() ? change.context : context);
                    if (!linked)
                    {
                        [[maybe_unused]] const auto cancelled = time_.Cancel(scheduled.Value());
                        return foundation::Result<std::uint64_t>::Failure(linked.GetError());
                    }
                }
            }
        }        else if (change.kind == conditions::ConditionChangeKind::Removed || change.kind == conditions::ConditionChangeKind::Expired)
        {
            if (change.expiration_schedule.has_value() && time_.HasSchedule(*change.expiration_schedule))
            {
                [[maybe_unused]] const auto cancelled = time_.Cancel(*change.expiration_schedule);
            }
            if (change.periodic_schedule.has_value() && time_.HasSchedule(*change.periodic_schedule))
            {
                [[maybe_unused]] const auto cancelled = time_.Cancel(*change.periodic_schedule);
            }
        }
        condition_cursor_ = change.sequence;
        ++processed;
    }
    return foundation::Result<std::uint64_t>::Success(processed);
}

foundation::Result<std::uint64_t> StateTimeAdapter::SynchronizeDeferredEffects(GameplayContext)
{
    std::uint64_t processed = 0;
    for (const auto& change : effects_.ChangesSince(effect_cursor_))
    {
        if (change.kind == effects::EffectChangeKind::DeferredCancelled && change.schedule.has_value() &&
            time_.HasSchedule(*change.schedule))
        {
            [[maybe_unused]] const auto cancelled = time_.Cancel(*change.schedule);
        }
        effect_cursor_ = change.sequence;
        ++processed;
    }

    for (const auto& deferred : effects_.UnscheduledDeferred())
    {
        const auto owner = GameplayObjectRef{effects::EffectService::Domain(), deferred.id.value};
        const auto persistence = deferred.persistence == effects::DeferredEffectPersistence::Persistent
                                     ? time::SchedulePersistence::Persistent
                                     : time::SchedulePersistence::Session;
        const auto scheduled = time_.Schedule(deferred.clock, deferred.due, owner, deferred_effect_action_, {},
                                              time::CatchUpPolicy::FireOnce, persistence);
        if (!scheduled)
        {
            return foundation::Result<std::uint64_t>::Failure(scheduled.GetError());
        }
        const auto bind = effects_.BindDeferredSchedule(deferred.id, scheduled.Value());
        if (!bind)
        {
            [[maybe_unused]] const auto cancelled = time_.Cancel(scheduled.Value());
            return foundation::Result<std::uint64_t>::Failure(bind.GetError());
        }
        ++processed;
    }
    return foundation::Result<std::uint64_t>::Success(processed);
}

foundation::Result<StateTimeProcessResult> StateTimeAdapter::ProcessDue(
    ClockId clock,
    GameplayContext context,
    time::SchedulerBudget scheduler_budget,
    effects::EffectExecutionBudget effect_budget,
    core::tasks::ITaskScheduler* scheduler)
{
    const auto triggers_result = time_.CollectDue(clock, scheduler_budget);
    if (!triggers_result)
    {
        return foundation::Result<StateTimeProcessResult>::Failure(triggers_result.GetError());
    }

    StateTimeProcessResult result;
    for (const auto& trigger : triggers_result.Value())
    {
        auto trigger_context = context;
        trigger_context.time = trigger.observed_at;
        if (trigger.action == expire_action_ && trigger.owner.domain == conditions::ConditionService::Domain())
        {
            const auto handled = conditions_.HandleExpirationDue(conditions::ConditionInstanceId{trigger.owner.id}, trigger.observed_at, trigger_context);
            if (handled)
            {
                ++result.condition_expirations;
            }
        }
        else if (trigger.action == periodic_action_ && trigger.owner.domain == conditions::ConditionService::Domain())
        {
            const auto handled = conditions_.HandlePeriodicDue(conditions::ConditionInstanceId{trigger.owner.id}, trigger.occurrence_count, trigger_context);
            if (!handled)
            {
                return foundation::Result<StateTimeProcessResult>::Failure(handled.GetError());
            }
            result.condition_periodic += trigger.occurrence_count;
        }
        else if (trigger.action == deferred_effect_action_ && trigger.owner.domain == effects::EffectService::Domain())
        {
            auto request = effects_.TakeDeferredBySchedule(trigger.schedule, trigger_context);
            if (!request)
            {
                return foundation::Result<StateTimeProcessResult>::Failure(request.GetError());
            }
            request.Value().context = trigger_context;
            auto executed = effects_.Execute(std::move(request).Value(), effect_budget, scheduler);
            if (!executed)
            {
                return foundation::Result<StateTimeProcessResult>::Failure(executed.GetError());
            }
            result.effect_executions.push_back(std::move(executed).Value());
        }
        else
        {
            result.unhandled.push_back(trigger);
        }
    }

    const auto condition_sync = SynchronizeConditionSchedules(context);
    if (!condition_sync)
    {
        return foundation::Result<StateTimeProcessResult>::Failure(condition_sync.GetError());
    }
    const auto effect_sync = SynchronizeDeferredEffects(context);
    if (!effect_sync)
    {
        return foundation::Result<StateTimeProcessResult>::Failure(effect_sync.GetError());
    }
    return foundation::Result<StateTimeProcessResult>::Success(std::move(result));
}
} // namespace epidemic::gameplay::state_integration







