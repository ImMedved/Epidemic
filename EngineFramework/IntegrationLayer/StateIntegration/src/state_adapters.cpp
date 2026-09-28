#include "Epidemic/GameFramework/StateIntegration/state_adapters.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>

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

template <typename TChange>
[[nodiscard]] bool HasJournalGap(std::uint64_t cursor, std::uint64_t latest, const std::vector<TChange>& changes)
{
    if (cursor >= latest)
    {
        return false;
    }
    if (changes.empty())
    {
        return true;
    }
    if (cursor == std::numeric_limits<std::uint64_t>::max())
    {
        return changes.front().sequence != 0;
    }
    return changes.front().sequence > cursor + 1;
}

[[nodiscard]] foundation::Error JournalGapError(std::string_view stream)
{
    return Error("gameplay.state_integration_journal_gap", std::string(stream) + " change journal has a gap; rebuild or durable delivery is required");
}

[[nodiscard]] bool ScheduleMatches(const time::ScheduleEntry& entry,
                                   ClockId clock,
                                   GameplayObjectRef owner,
                                   ActionTypeId action,
                                   time::SchedulePersistence persistence) noexcept
{
    return entry.clock == clock && entry.owner == owner && entry.action == action && entry.persistence == persistence;
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
        const effects::RegisteredEffectPayload&) noexcept override
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
        const effects::RegisteredEffectPayload&) noexcept override
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
        const effects::RegisteredEffectPayload&) noexcept override
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
        const effects::RegisteredEffectPayload&) noexcept override
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
        const effects::RegisteredEffectPayload&) noexcept override
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
        const effects::RegisteredEffectPayload&) noexcept override
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
        const effects::RegisteredEffectPayload&) noexcept override
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
        const effects::RegisteredEffectPayload&) noexcept override
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

foundation::Result<std::uint64_t> StateFactsAdapter::RebuildActiveConditionFacts(GameplayContext context)
{
    struct DesiredConditionFact
    {
        facts::FactKey key{};
        ConditionFactValue value{};
        facts::FactPersistence persistence = facts::FactPersistence::Session;
        std::optional<GameplayTimePoint> expires_at{};
    };

    std::vector<DesiredConditionFact> desired;
    for (const auto& instance : conditions_.AllConditions())
    {
        const auto* definition = conditions_.FindDefinition(instance.type);
        if (definition == nullptr || !definition->publish_fact)
        {
            continue;
        }
        const auto persistence = instance.expires_at.has_value()
                                     ? facts::FactPersistence::Timed
                                     : (definition->persistence == conditions::ConditionPersistencePolicy::Persistent
                                            ? facts::FactPersistence::Persistent
                                            : facts::FactPersistence::Session);
        desired.push_back(DesiredConditionFact{
            facts::FactKey{active_condition_fact_, instance.subject, ConditionScope(instance.id)},
            ConditionFactValue{instance.type, instance.magnitude_micro, instance.stacks, instance.paused_for_materialization},
            persistence,
            instance.expires_at});
    }

    auto transaction = facts_.BeginTransaction(conditions::ConditionService::Domain());
    std::uint64_t planned = 0;

    for (const auto& record : facts_.FindFacts(active_condition_fact_, {}, {}))
    {
        const bool still_desired = std::any_of(desired.begin(), desired.end(), [&record](const DesiredConditionFact& item) {
            return item.key == record.key;
        });
        if (!still_desired)
        {
            transaction.Remove(record.key);
            ++planned;
        }
    }

    for (const auto& item : desired)
    {
        transaction.Set(item.key.type, item.key.subject, item.key.scope, item.value, item.persistence, item.expires_at);
        ++planned;
    }

    if (transaction.Empty())
    {
        return foundation::Result<std::uint64_t>::Success(0);
    }
    const auto committed = facts_.Commit(std::move(transaction), context);
    if (!committed)
    {
        return foundation::Result<std::uint64_t>::Failure(committed.GetError());
    }
    return foundation::Result<std::uint64_t>::Success(committed.Value().size() == 0 ? 0 : planned);
}

foundation::Result<std::uint64_t> StateFactsAdapter::PublishPendingChanges(GameplayContext context)
{
    std::uint64_t published = 0;
    const auto producer = ProducerId::FromString("framework.state_integration");

    const auto entity_batch = entities_.ReadChangesSince(entity_cursor_);
    if (entity_batch.snapshot_required)
    {
        return foundation::Result<std::uint64_t>::Failure(JournalGapError("entities"));
    }
    if (!entity_batch.changes.empty())
    {
        auto batch = facts_.CreateBatch(producer, entity_cursor_.sequence + 1);
        for (const auto& change : entity_batch.changes)
        {
            auto event_context = change.context.tick.IsValid() ? change.context : context;
            batch.Publish(entity_changed_, event_context, entities::EntityService::ToGameplayObjectRef(change.entity), change);
        }
        const auto submitted = facts_.SubmitBatch(std::move(batch));
        if (!submitted)
        {
            return foundation::Result<std::uint64_t>::Failure(submitted.GetError());
        }
        entity_cursor_ = {entity_batch.latest_cursor.epoch, entity_batch.changes.back().sequence};
        published += entity_batch.changes.size();
    }

    const auto material_batch = materials_.ReadChangesSince(material_cursor_);
    if (material_batch.snapshot_required)
    {
        return foundation::Result<std::uint64_t>::Failure(JournalGapError("materials"));
    }
    if (!material_batch.changes.empty())
    {
        auto batch = facts_.CreateBatch(producer, material_cursor_.sequence + 1);
        for (const auto& change : material_batch.changes)
        {
            auto event_context = change.context.tick.IsValid() ? change.context : context;
            batch.Publish(material_changed_, event_context, change.key.subject, change);
        }
        const auto submitted = facts_.SubmitBatch(std::move(batch));
        if (!submitted)
        {
            return foundation::Result<std::uint64_t>::Failure(submitted.GetError());
        }
        material_cursor_ = {material_batch.latest_cursor.epoch, material_batch.changes.back().sequence};
        published += material_batch.changes.size();
    }

    struct PendingConditionFactMutation
    {
        facts::FactKey key{};
        conditions::ConditionChange change{};
    };

    const auto condition_batch = conditions_.ReadChangesSince(condition_cursor_);
    const bool condition_snapshot_required = condition_batch.snapshot_required;
    if (condition_snapshot_required)
    {
        const auto rebuilt = RebuildActiveConditionFacts(context);
        if (!rebuilt)
        {
            return foundation::Result<std::uint64_t>::Failure(rebuilt.GetError());
        }
        condition_cursor_ = conditions_.LatestChangeCursor();
        published += rebuilt.Value();
    }
    else if (!condition_batch.changes.empty())
    {
        std::vector<PendingConditionFactMutation> pending_fact_mutations;
        std::unordered_map<facts::FactKey, std::size_t, facts::FactKeyHash> pending_fact_indices;

        for (const auto& change : condition_batch.changes)
        {
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

        auto batch = facts_.CreateBatch(producer, condition_cursor_.sequence + 1);
        for (const auto& change : condition_batch.changes)
        {
            auto event_context = change.context.tick.IsValid() ? change.context : context;
            batch.Publish(condition_changed_, event_context, change.subject, change);
        }
        const auto submitted = facts_.SubmitBatch(std::move(batch));
        if (!submitted)
        {
            return foundation::Result<std::uint64_t>::Failure(submitted.GetError());
        }
        condition_cursor_ = {condition_batch.latest_cursor.epoch, condition_batch.changes.back().sequence};
        published += condition_batch.changes.size();
    }

    const auto effect_batch = effects_.ReadChangesSince(effect_cursor_);
    if (effect_batch.snapshot_required)
    {
        return foundation::Result<std::uint64_t>::Failure(JournalGapError("effects"));
    }
    if (!effect_batch.changes.empty())
    {
        auto batch = facts_.CreateBatch(producer, effect_cursor_.sequence + 1);
        for (const auto& change : effect_batch.changes)
        {
            auto event_context = change.context.tick.IsValid() ? change.context : context;
            batch.Publish(effect_changed_, event_context, change.target, change);
        }
        const auto submitted = facts_.SubmitBatch(std::move(batch));
        if (!submitted)
        {
            return foundation::Result<std::uint64_t>::Failure(submitted.GetError());
        }
        effect_cursor_ = {effect_batch.latest_cursor.epoch, effect_batch.changes.back().sequence};
        published += effect_batch.changes.size();
    }

    if (!entity_cursor_.IsValid()) entity_cursor_.epoch = entity_batch.latest_cursor.epoch;
    if (!material_cursor_.IsValid()) material_cursor_.epoch = material_batch.latest_cursor.epoch;
    if (!condition_cursor_.IsValid()) condition_cursor_.epoch = condition_batch.latest_cursor.epoch;
    if (!effect_cursor_.IsValid()) effect_cursor_.epoch = effect_batch.latest_cursor.epoch;
    return foundation::Result<std::uint64_t>::Success(published);
}

std::uint64_t StateFactsAdapter::ContractRevision() const noexcept
{
    std::uint64_t hash = 0xCBF29CE484222325ull;
    const std::uint64_t ids[] = {
        entity_changed_.Raw(), material_changed_.Raw(), condition_changed_.Raw(), effect_changed_.Raw(), active_condition_fact_.Raw()};
    for (const auto id : ids)
    {
        hash ^= id;
        hash *= 0x100000001B3ull;
    }
    return hash == 0 ? 1 : hash;
}

StateFactsCheckpoint StateFactsAdapter::CaptureCheckpoint() const noexcept
{
    StateFactsCheckpoint checkpoint;
    checkpoint.contract_revision = ContractRevision();
    checkpoint.entity_cursor = entity_cursor_;
    checkpoint.material_cursor = material_cursor_;
    checkpoint.condition_cursor = condition_cursor_;
    checkpoint.effect_cursor = effect_cursor_;
    checkpoint.entity_latest = entities_.LatestChangeCursor();
    checkpoint.material_latest = materials_.LatestChangeCursor();
    checkpoint.condition_latest = conditions_.LatestChangeCursor();
    checkpoint.effect_latest = effects_.LatestChangeCursor();
    return checkpoint;
}

foundation::Result<void> StateFactsAdapter::ValidateCheckpoint(const StateFactsCheckpoint& checkpoint) const
{
    const auto valid_pair = [](ChangeCursor cursor, ChangeCursor latest) {
        return cursor.IsValid() && cursor.epoch == latest.epoch && cursor.sequence <= latest.sequence;
    };
    const auto current_condition = conditions_.LatestChangeCursor();
    if (checkpoint.schema_version != 2 || checkpoint.contract_revision != ContractRevision() ||
        !valid_pair(checkpoint.entity_cursor, checkpoint.entity_latest) ||
        !valid_pair(checkpoint.material_cursor, checkpoint.material_latest) ||
        !valid_pair(checkpoint.condition_cursor, checkpoint.condition_latest) ||
        !valid_pair(checkpoint.effect_cursor, checkpoint.effect_latest) ||
        checkpoint.condition_latest.sequence > current_condition.sequence)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.state_facts_checkpoint_invalid", "state facts checkpoint is incompatible with the current contracts or journals"));
    }
    return foundation::Result<void>::Success();
}

void StateFactsAdapter::ApplyCheckpoint(StateFactsCheckpoint checkpoint) noexcept
{
    const auto entity_latest = entities_.LatestChangeCursor();
    const auto material_latest = materials_.LatestChangeCursor();
    const auto condition_latest = conditions_.LatestChangeCursor();
    const auto effect_latest = effects_.LatestChangeCursor();
    const auto translate = [](ChangeCursor current, ChangeCursor saved_latest, ChangeCursor saved_cursor) {
        return current.sequence >= saved_latest.sequence
                   ? ChangeCursor{current.epoch, saved_cursor.sequence}
                   : ChangeCursor{current.epoch, 0};
    };
    entity_cursor_ = translate(entity_latest, checkpoint.entity_latest, checkpoint.entity_cursor);
    material_cursor_ = translate(material_latest, checkpoint.material_latest, checkpoint.material_cursor);
    condition_cursor_ = translate(condition_latest, checkpoint.condition_latest, checkpoint.condition_cursor);
    effect_cursor_ = translate(effect_latest, checkpoint.effect_latest, checkpoint.effect_cursor);
}

foundation::Result<void> StateFactsAdapter::RestoreCheckpoint(StateFactsCheckpoint checkpoint)
{
    const auto valid = ValidateCheckpoint(checkpoint);
    if (!valid)
        return valid;
    ApplyCheckpoint(std::move(checkpoint));
    return foundation::Result<void>::Success();
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
    auto cleanup_destroyed_entity = [this, context](entities::EntityId entity, GameplayContext change_context)
        -> foundation::Result<void> {
        const auto ref = entities::EntityService::ToGameplayObjectRef(entity);
        const auto effective_context = change_context.tick.IsValid() ? change_context : context;
        const auto removed_materials = materials_.RemoveSubject(ref, effective_context);
        if (!removed_materials)
        {
            return foundation::Result<void>::Failure(removed_materials.GetError());
        }
        (void)conditions_.RemoveSubject(ref, conditions::ConditionRemovalReason::SubjectDestroyed, effective_context);
        (void)effects_.CancelDeferredTargeting(ref, effective_context);
        return foundation::Result<void>::Success();
    };

    const auto batch = entities_.ReadChangesSince(cursor_);
    if (batch.snapshot_required)
    {
        std::uint64_t reconciled = 0;
        for (const auto& entity : entities_.AllEntities())
        {
            if (entity.lifecycle == entities::EntityLifecycleState::Destroyed || entity.lifecycle == entities::EntityLifecycleState::Removed)
            {
                const auto cleanup = cleanup_destroyed_entity(entity.id, context);
                if (!cleanup)
                {
                    return foundation::Result<std::uint64_t>::Failure(cleanup.GetError());
                }
                ++reconciled;
            }
        }
        cursor_ = entities_.LatestChangeCursor();
        return foundation::Result<std::uint64_t>::Success(reconciled);
    }

    std::uint64_t processed = 0;
    ChangeCursor next_cursor = cursor_;
    next_cursor.epoch = batch.latest_cursor.epoch;
    for (const auto& change : batch.changes)
    {
        const auto ref = entities::EntityService::ToGameplayObjectRef(change.entity);
        const auto effective_context = change.context.tick.IsValid() ? change.context : context;
        if (change.kind == entities::EntityChangeKind::Destroyed || change.kind == entities::EntityChangeKind::Removed)
        {
            const auto cleanup = cleanup_destroyed_entity(change.entity, effective_context);
            if (!cleanup)
            {
                return foundation::Result<std::uint64_t>::Failure(cleanup.GetError());
            }
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
        next_cursor.sequence = change.sequence;
        ++processed;
    }
    cursor_ = next_cursor;
    return foundation::Result<std::uint64_t>::Success(processed);
}

StateLifecycleCheckpoint StateLifecycleAdapter::CaptureCheckpoint() const noexcept
{
    return StateLifecycleCheckpoint{2, cursor_, entities_.LatestChangeCursor()};
}

foundation::Result<void> StateLifecycleAdapter::ValidateCheckpoint(const StateLifecycleCheckpoint& checkpoint) const
{
    if (checkpoint.schema_version != 2 || !checkpoint.cursor.IsValid() ||
        checkpoint.cursor.epoch != checkpoint.entity_latest.epoch ||
        checkpoint.cursor.sequence > checkpoint.entity_latest.sequence)
        return foundation::Result<void>::Failure(
            Error("gameplay.state_lifecycle_checkpoint_invalid", "state lifecycle checkpoint is invalid"));
    return foundation::Result<void>::Success();
}

void StateLifecycleAdapter::ApplyCheckpoint(StateLifecycleCheckpoint checkpoint) noexcept
{
    const auto latest = entities_.LatestChangeCursor();
    cursor_ = latest == checkpoint.entity_latest ? checkpoint.cursor : ChangeCursor{latest.epoch, 0};
}

foundation::Result<void> StateLifecycleAdapter::RestoreCheckpoint(StateLifecycleCheckpoint checkpoint)
{
    const auto valid = ValidateCheckpoint(checkpoint);
    if (!valid)
        return valid;
    ApplyCheckpoint(std::move(checkpoint));
    return foundation::Result<void>::Success();
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
    effects::EffectExecutionBudget budget)
{
    std::vector<effects::EffectExecutionResult> results;
    const auto batch = conditions_.ReadChangesSince(cursor_);
    if (batch.snapshot_required)
    {
        return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(JournalGapError("conditions"));
    }
    cursor_.epoch = batch.latest_cursor.epoch;

    const auto route_revision = RouteRevision();
    for (const auto& change : batch.changes)
    {
        const auto* definition = conditions_.FindDefinition(change.type);
        if (definition == nullptr)
        {
            cursor_.sequence = change.sequence;
            PruneTerminalDeliveries();
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
        if (!action.IsValid() || route == routes_.end())
        {
            cursor_.sequence = change.sequence;
            PruneTerminalDeliveries();
            continue;
        }

        const ConditionEffectDeliveryKey key{change.sequence, action, route->second, route_revision};
        auto* delivery = FindDelivery(change.sequence);
        if (delivery == nullptr)
        {
            if (deliveries_.size() >= kDeliveryCapacity)
                return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
                    Error("gameplay.condition_effect_delivery_capacity", "condition effect delivery ledger capacity is exhausted"));
            deliveries_.push_back(ConditionEffectDeliveryRecord{key});
            delivery = &deliveries_.back();
        }
        else if (!(delivery->key == key))
        {
            return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
                Error("gameplay.condition_effect_route_changed", "condition effect delivery route changed for an unresolved condition change"));
        }

        if (delivery->state == ConditionEffectDeliveryState::Applied ||
            delivery->state == ConditionEffectDeliveryState::RejectedTerminal)
        {
            cursor_.sequence = change.sequence;
            PruneTerminalDeliveries();
            continue;
        }
        if (delivery->state == ConditionEffectDeliveryState::ReconciliationRequired)
        {
            return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
                Error("gameplay.condition_effect_reconciliation_required", "condition effect delivery requires explicit reconciliation"));
        }

        delivery->retry_authorized = false;
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

        auto executed = effects_.Execute(std::move(request), budget);
        if (!executed)
        {
            delivery->state = ConditionEffectDeliveryState::ReconciliationRequired;
            delivery->last_disposition = effects::EffectBatchDisposition::Failed;
            delivery->had_applied_operation = false;
            return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(executed.GetError());
        }

        auto execution = std::move(executed).Value();
        const bool had_applied = std::any_of(execution.operations.begin(), execution.operations.end(), [](const auto& operation) {
            return operation.disposition == effects::EffectOperationDisposition::Applied;
        });
        delivery->last_execution = execution.execution;
        delivery->last_disposition = execution.disposition;
        delivery->had_applied_operation = had_applied;

        const auto disposition = execution.disposition;
        results.push_back(std::move(execution));
        if (disposition == effects::EffectBatchDisposition::Succeeded)
        {
            delivery->state = ConditionEffectDeliveryState::Applied;
            cursor_.sequence = change.sequence;
            PruneTerminalDeliveries();
            continue;
        }
        if (disposition == effects::EffectBatchDisposition::Rejected && !had_applied)
        {
            delivery->state = ConditionEffectDeliveryState::RejectedTerminal;
            cursor_.sequence = change.sequence;
            PruneTerminalDeliveries();
            continue;
        }
        if (disposition == effects::EffectBatchDisposition::Failed && !had_applied && !results.back().operations.empty())
        {
            delivery->state = ConditionEffectDeliveryState::Pending;
            delivery->retry_authorized = true;
            break;
        }

        delivery->state = ConditionEffectDeliveryState::ReconciliationRequired;
        return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
            Error("gameplay.condition_effect_reconciliation_required", "condition effect batch crossed or may have crossed the commit boundary"));
    }
    return foundation::Result<std::vector<effects::EffectExecutionResult>>::Success(std::move(results));
}

std::uint64_t ConditionEffectsAdapter::RouteRevision() const noexcept
{
    std::uint64_t hash = 0xCBF29CE484222325ull ^ static_cast<std::uint64_t>(routes_.size());
    for (const auto& [action, definition] : routes_)
    {
        const auto pair_hash = (action.Raw() * 0x9E3779B97F4A7C15ull) ^
                               (definition.Raw() * 0xD6E8FEB86659FD93ull);
        hash ^= pair_hash;
    }
    return hash == 0 ? 1 : hash;
}

ConditionEffectDeliveryRecord* ConditionEffectsAdapter::FindDelivery(std::uint64_t condition_sequence) noexcept
{
    const auto found = std::find_if(deliveries_.begin(), deliveries_.end(), [condition_sequence](const auto& delivery) {
        return delivery.key.condition_sequence == condition_sequence;
    });
    return found == deliveries_.end() ? nullptr : &*found;
}

const ConditionEffectDeliveryRecord* ConditionEffectsAdapter::FindDelivery(std::uint64_t condition_sequence) const noexcept
{
    const auto found = std::find_if(deliveries_.begin(), deliveries_.end(), [condition_sequence](const auto& delivery) {
        return delivery.key.condition_sequence == condition_sequence;
    });
    return found == deliveries_.end() ? nullptr : &*found;
}

void ConditionEffectsAdapter::PruneTerminalDeliveries()
{
    std::erase_if(deliveries_, [this](const auto& delivery) {
        return delivery.key.condition_sequence <= cursor_.sequence &&
               (delivery.state == ConditionEffectDeliveryState::Applied ||
                delivery.state == ConditionEffectDeliveryState::RejectedTerminal);
    });
}

ConditionEffectsCheckpoint ConditionEffectsAdapter::CaptureCheckpoint() const
{
    ConditionEffectsCheckpoint checkpoint;
    checkpoint.cursor = cursor_;
    checkpoint.condition_latest = conditions_.LatestChangeCursor();
    checkpoint.route_revision = RouteRevision();
    checkpoint.deliveries = deliveries_;
    std::sort(checkpoint.deliveries.begin(), checkpoint.deliveries.end(), [](const auto& left, const auto& right) {
        return left.key.condition_sequence < right.key.condition_sequence;
    });
    return checkpoint;
}

foundation::Result<void> ConditionEffectsAdapter::ValidateCheckpoint(const ConditionEffectsCheckpoint& checkpoint) const
{
    const auto current_latest = conditions_.LatestChangeCursor();
    if (checkpoint.schema_version != 3 || checkpoint.route_revision != RouteRevision() ||
        !checkpoint.cursor.IsValid() || checkpoint.cursor.epoch != checkpoint.condition_latest.epoch ||
        checkpoint.cursor.sequence > checkpoint.condition_latest.sequence ||
        checkpoint.condition_latest.sequence > current_latest.sequence ||
        checkpoint.deliveries.size() > kDeliveryCapacity)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.condition_effect_checkpoint_invalid", "condition effect checkpoint is incompatible with routes or condition journal"));
    }

    std::uint64_t previous_sequence = 0;
    for (const auto& delivery : checkpoint.deliveries)
    {
        const auto& key = delivery.key;
        const auto route = routes_.find(key.action);
        if (key.condition_sequence == 0 || key.condition_sequence <= checkpoint.cursor.sequence ||
            key.condition_sequence > checkpoint.condition_latest.sequence || key.condition_sequence <= previous_sequence ||
            key.route_revision != checkpoint.route_revision || !key.action.IsValid() || !key.definition.IsValid() ||
            route == routes_.end() || route->second != key.definition ||
            (delivery.retry_authorized && delivery.state != ConditionEffectDeliveryState::Pending))
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.condition_effect_checkpoint_invalid", "condition effect checkpoint contains an invalid delivery record"));
        }
        previous_sequence = key.condition_sequence;
    }
    return foundation::Result<void>::Success();
}

void ConditionEffectsAdapter::ApplyCheckpoint(ConditionEffectsCheckpoint checkpoint) noexcept
{
    const auto current_latest = conditions_.LatestChangeCursor();
    cursor_ = current_latest.sequence >= checkpoint.condition_latest.sequence
                  ? ChangeCursor{current_latest.epoch, checkpoint.cursor.sequence}
                  : ChangeCursor{current_latest.epoch, 0};
    deliveries_ = std::move(checkpoint.deliveries);
}

foundation::Result<void> ConditionEffectsAdapter::RestoreCheckpoint(ConditionEffectsCheckpoint checkpoint)
{
    std::sort(checkpoint.deliveries.begin(), checkpoint.deliveries.end(), [](const auto& left, const auto& right) {
        return left.key.condition_sequence < right.key.condition_sequence;
    });
    const auto valid = ValidateCheckpoint(checkpoint);
    if (!valid)
        return valid;
    ApplyCheckpoint(std::move(checkpoint));
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConditionEffectsAdapter::ResolveReconciliation(
    const ConditionEffectDeliveryKey& key,
    ConditionEffectReconciliationResolution resolution)
{
    if (key.route_revision != RouteRevision())
        return foundation::Result<void>::Failure(
            Error("gameplay.condition_effect_reconciliation_invalid", "condition effect reconciliation route revision is stale"));
    const auto route = routes_.find(key.action);
    if (route == routes_.end() || route->second != key.definition)
        return foundation::Result<void>::Failure(
            Error("gameplay.condition_effect_reconciliation_invalid", "condition effect reconciliation route is invalid"));

    auto* delivery = FindDelivery(key.condition_sequence);
    if (delivery == nullptr)
    {
        if (key.condition_sequence <= cursor_.sequence && resolution != ConditionEffectReconciliationResolution::ConfirmedNotApplied)
            return foundation::Result<void>::Success();
        return foundation::Result<void>::Failure(
            Error("gameplay.condition_effect_reconciliation_missing", "condition effect reconciliation delivery is missing"));
    }
    if (!(delivery->key == key))
        return foundation::Result<void>::Failure(
            Error("gameplay.condition_effect_reconciliation_invalid", "condition effect reconciliation key does not match delivery"));

    if (delivery->state != ConditionEffectDeliveryState::ReconciliationRequired)
    {
        const bool idempotent_applied = resolution == ConditionEffectReconciliationResolution::ConfirmedApplied &&
                                        delivery->state == ConditionEffectDeliveryState::Applied;
        const bool idempotent_rejected = resolution == ConditionEffectReconciliationResolution::RejectedTerminal &&
                                         delivery->state == ConditionEffectDeliveryState::RejectedTerminal;
        const bool idempotent_retry = resolution == ConditionEffectReconciliationResolution::ConfirmedNotApplied &&
                                      delivery->state == ConditionEffectDeliveryState::Pending && delivery->retry_authorized;
        if (idempotent_applied || idempotent_rejected || idempotent_retry)
            return foundation::Result<void>::Success();
        return foundation::Result<void>::Failure(
            Error("gameplay.condition_effect_reconciliation_invalid", "only reconciliation-required deliveries can be resolved"));
    }

    switch (resolution)
    {
    case ConditionEffectReconciliationResolution::ConfirmedApplied:
        delivery->state = ConditionEffectDeliveryState::Applied;
        delivery->retry_authorized = false;
        break;
    case ConditionEffectReconciliationResolution::ConfirmedNotApplied:
        delivery->state = ConditionEffectDeliveryState::Pending;
        delivery->retry_authorized = true;
        break;
    case ConditionEffectReconciliationResolution::RejectedTerminal:
        delivery->state = ConditionEffectDeliveryState::RejectedTerminal;
        delivery->retry_authorized = false;
        break;
    }
    return foundation::Result<void>::Success();
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

foundation::Result<void> StateTimeAdapter::RegisterWithDispatcher(
    integration::ScheduledTriggerDispatcher& dispatcher,
    effects::EffectExecutionBudget effect_budget)
{
    if (!expire_action_.IsValid() || !periodic_action_.IsValid() || !deferred_effect_action_.IsValid())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.state_time_contracts_missing", "state time contracts must be registered before dispatcher handlers"));
    }
    if (dispatcher_ != nullptr)
    {
        return dispatcher_ == &dispatcher
                   ? foundation::Result<void>::Success()
                   : foundation::Result<void>::Failure(
                         Error("gameplay.state_time_dispatcher_conflict", "state time adapter is already bound to another dispatcher"));
    }

    auto expiration = dispatcher.RegisterActionHandler(
        expire_action_, TypeId::FromString("framework.state_time.expiration.handler"),
        [this](const time::ScheduledTrigger& trigger, const GameplayContext& context) {
            return HandleExpirationTrigger(trigger, context);
        });
    if (!expiration)
        return expiration;
    auto periodic = dispatcher.RegisterActionHandler(
        periodic_action_, TypeId::FromString("framework.state_time.periodic.handler"),
        [this](const time::ScheduledTrigger& trigger, const GameplayContext& context) {
            return HandlePeriodicTrigger(trigger, context);
        });
    if (!periodic)
        return periodic;
    auto deferred = dispatcher.RegisterActionHandler(
        deferred_effect_action_, TypeId::FromString("framework.state_time.deferred_effect.handler"),
        [this](const time::ScheduledTrigger& trigger, const GameplayContext& context) {
            return HandleDeferredEffectTrigger(trigger, context);
        });
    if (!deferred)
        return deferred;

    dispatcher_ = &dispatcher;
    effect_budget_ = effect_budget;
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

    auto cancel_if_present = [this](std::optional<ScheduleId> schedule) -> foundation::Result<void> {
        if (schedule.has_value() && time_.HasSchedule(*schedule))
        {
            const auto cancelled = time_.Cancel(*schedule);
            if (!cancelled)
            {
                return foundation::Result<void>::Failure(cancelled.GetError());
            }
        }
        return foundation::Result<void>::Success();
    };

    const auto cancelled_expiration = cancel_if_present(instance.expiration_schedule);
    if (!cancelled_expiration)
    {
        return foundation::Result<void>::Failure(cancelled_expiration.GetError());
    }
    const auto cancelled_periodic = cancel_if_present(instance.periodic_schedule);
    if (!cancelled_periodic)
    {
        return foundation::Result<void>::Failure(cancelled_periodic.GetError());
    }

    std::optional<ScheduleId> expiration;
    std::optional<ScheduleId> periodic;
    const auto owner = GameplayObjectRef{conditions::ConditionService::Domain(), instance.id.value};
    if (instance.expires_at.has_value() && !instance.paused_for_materialization)
    {
        const auto scheduled = time_.Schedule(definition->clock, *instance.expires_at, owner, expire_action_, {},
                                              time::CatchUpPolicy::FireOnce, MapPersistence(definition->persistence));
        if (!scheduled)
        {
            return foundation::Result<void>::Failure(scheduled.GetError());
        }
        expiration = scheduled.Value();
    }
    if (definition->periodic_interval.ticks > 0 && !instance.paused_for_materialization)
    {
        const auto due = ::epidemic::gameplay::CheckedAdd(instance.applied_at, definition->periodic_interval);
        if (!due.has_value())
        {
            if (expiration.has_value() && time_.HasSchedule(*expiration))
            {
                (void)time_.Cancel(*expiration);
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
                (void)time_.Cancel(*expiration);
            }
            return foundation::Result<void>::Failure(scheduled.GetError());
        }
        periodic = scheduled.Value();
    }
    const auto linked = conditions_.SetScheduleLinks(instance.id, expiration, periodic, context);
    if (!linked)
    {
        if (expiration.has_value() && time_.HasSchedule(*expiration))
        {
            (void)time_.Cancel(*expiration);
        }
        if (periodic.has_value() && time_.HasSchedule(*periodic))
        {
            (void)time_.Cancel(*periodic);
        }
        return foundation::Result<void>::Failure(linked.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<std::uint64_t> StateTimeAdapter::ReconcileConditionSchedules(GameplayContext context)
{
    const auto schedules = time_.CaptureSnapshot().schedules;
    std::uint64_t reconciled = 0;

    auto adopt = [&](GameplayObjectRef owner, ActionTypeId action, auto&& predicate)
        -> foundation::Result<std::optional<ScheduleId>> {
        std::vector<ScheduleId> matches;
        for (const auto& entry : schedules)
        {
            if (entry.owner == owner && entry.action == action && predicate(entry) && time_.HasSchedule(entry.id))
                matches.push_back(entry.id);
        }
        std::sort(matches.begin(), matches.end());
        for (std::size_t index = 1; index < matches.size(); ++index)
        {
            const auto cancelled = time_.Cancel(matches[index]);
            if (!cancelled)
                return foundation::Result<std::optional<ScheduleId>>::Failure(cancelled.GetError());
            ++reconciled;
        }
        return foundation::Result<std::optional<ScheduleId>>::Success(
            matches.empty() ? std::nullopt : std::optional<ScheduleId>{matches.front()});
    };

    for (const auto& instance : conditions_.AllConditions())
    {
        const auto* definition = conditions_.FindDefinition(instance.type);
        if (definition == nullptr)
            return foundation::Result<std::uint64_t>::Failure(
                Error("gameplay.condition_unknown", "condition definition is missing while reconciling schedules"));

        const auto owner = GameplayObjectRef{conditions::ConditionService::Domain(), instance.id.value};
        const auto persistence = MapPersistence(definition->persistence);
        auto expiration = instance.expiration_schedule;
        auto periodic = instance.periodic_schedule;

        const bool wants_expiration = instance.expires_at.has_value() && !instance.paused_for_materialization;
        if (expiration.has_value())
        {
            const auto entry = time_.GetSchedule(*expiration);
            if (!wants_expiration || !entry.has_value() ||
                !ScheduleMatches(*entry, definition->clock, owner, expire_action_, persistence) ||
                entry->due != *instance.expires_at || entry->recurrence.kind != time::RecurrenceKind::Once)
            {
                if (entry.has_value())
                {
                    const auto cancelled = time_.Cancel(*expiration);
                    if (!cancelled)
                        return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
                }
                expiration.reset();
                ++reconciled;
            }
        }
        if (wants_expiration && !expiration.has_value())
        {
            auto restored = adopt(owner, expire_action_, [&](const time::ScheduleEntry& entry) {
                return ScheduleMatches(entry, definition->clock, owner, expire_action_, persistence) &&
                       entry.due == *instance.expires_at && entry.recurrence.kind == time::RecurrenceKind::Once;
            });
            if (!restored)
                return foundation::Result<std::uint64_t>::Failure(restored.GetError());
            expiration = restored.Value();
            if (expiration.has_value())
                ++reconciled;
            else if (!HasPendingOccurrence(owner, expire_action_))
            {
                auto scheduled = time_.Schedule(definition->clock, *instance.expires_at, owner, expire_action_, {},
                                                time::CatchUpPolicy::FireOnce, persistence);
                if (!scheduled)
                    return foundation::Result<std::uint64_t>::Failure(scheduled.GetError());
                expiration = scheduled.Value();
                ++reconciled;
            }
        }

        const bool wants_periodic = definition->periodic_interval.ticks > 0 && !instance.paused_for_materialization;
        if (periodic.has_value())
        {
            const auto entry = time_.GetSchedule(*periodic);
            const bool valid = entry.has_value() &&
                               ScheduleMatches(*entry, definition->clock, owner, periodic_action_, persistence) &&
                               entry->recurrence.kind == time::RecurrenceKind::FixedInterval &&
                               entry->recurrence.interval == definition->periodic_interval &&
                               entry->catch_up == MapCatchUp(definition->periodic_catch_up);
            if (!wants_periodic || !valid)
            {
                if (entry.has_value())
                {
                    const auto cancelled = time_.Cancel(*periodic);
                    if (!cancelled)
                        return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
                }
                periodic.reset();
                ++reconciled;
            }
        }
        if (wants_periodic && !periodic.has_value())
        {
            auto restored = adopt(owner, periodic_action_, [&](const time::ScheduleEntry& entry) {
                return ScheduleMatches(entry, definition->clock, owner, periodic_action_, persistence) &&
                       entry.recurrence.kind == time::RecurrenceKind::FixedInterval &&
                       entry.recurrence.interval == definition->periodic_interval &&
                       entry.catch_up == MapCatchUp(definition->periodic_catch_up);
            });
            if (!restored)
                return foundation::Result<std::uint64_t>::Failure(restored.GetError());
            periodic = restored.Value();
            if (periodic.has_value())
                ++reconciled;
            else if (!HasPendingOccurrence(owner, periodic_action_))
            {
                const auto due = CheckedAdd(instance.applied_at, definition->periodic_interval);
                if (!due.has_value())
                    return foundation::Result<std::uint64_t>::Failure(
                        Error("gameplay.time_overflow", "condition periodic schedule overflows gameplay time"));
                time::RecurrenceRule recurrence;
                recurrence.kind = time::RecurrenceKind::FixedInterval;
                recurrence.interval = definition->periodic_interval;
                auto scheduled = time_.Schedule(definition->clock, *due, owner, periodic_action_, recurrence,
                                                MapCatchUp(definition->periodic_catch_up), persistence);
                if (!scheduled)
                    return foundation::Result<std::uint64_t>::Failure(scheduled.GetError());
                periodic = scheduled.Value();
                ++reconciled;
            }
        }

        if (expiration != instance.expiration_schedule || periodic != instance.periodic_schedule)
        {
            const auto linked = conditions_.SetScheduleLinks(instance.id, expiration, periodic, context);
            if (!linked)
                return foundation::Result<std::uint64_t>::Failure(linked.GetError());
        }
    }
    return foundation::Result<std::uint64_t>::Success(reconciled);
}

foundation::Result<std::uint64_t> StateTimeAdapter::SynchronizeConditionSchedules(GameplayContext context)
{
    std::uint64_t processed = 0;
    const auto batch = conditions_.ReadChangesSince(condition_cursor_);
    const bool condition_snapshot_required = batch.snapshot_required;
    if (condition_snapshot_required)
    {
        const auto reconciled = ReconcileConditionSchedules(context);
        if (!reconciled)
        {
            return foundation::Result<std::uint64_t>::Failure(reconciled.GetError());
        }
        condition_cursor_ = conditions_.LatestChangeCursor();
        return foundation::Result<std::uint64_t>::Success(reconciled.Value());
    }

    ChangeCursor next_cursor = condition_cursor_;
    next_cursor.epoch = batch.latest_cursor.epoch;
    for (const auto& change : batch.changes)
    {
        if (change.kind == conditions::ConditionChangeKind::Removed ||
            change.kind == conditions::ConditionChangeKind::Expired)
        {
            const auto owner = GameplayObjectRef{conditions::ConditionService::Domain(), change.instance.value};
            (void)time_.CancelOwnedBy(owner);
        }
        next_cursor.sequence = change.sequence;
        ++processed;
    }
    condition_cursor_ = next_cursor;

    const auto reconciled = ReconcileConditionSchedules(context);
    if (!reconciled)
    {
        return foundation::Result<std::uint64_t>::Failure(reconciled.GetError());
    }
    return foundation::Result<std::uint64_t>::Success(processed + reconciled.Value());
}

foundation::Result<std::uint64_t> StateTimeAdapter::ReconcileDeferredEffectSchedules(GameplayContext context)
{
    (void)context;
    const auto schedules = time_.CaptureSnapshot().schedules;
    const auto pending = dispatcher_ == nullptr
                             ? integration::ScheduledTriggerDispatcherSnapshot{}
                             : dispatcher_->CaptureSnapshot();
    std::uint64_t reconciled = 0;

    for (const auto& deferred : effects_.AllDeferred())
    {
        if (std::any_of(deferred_reconciliations_.begin(), deferred_reconciliations_.end(),
                        [&](const auto& record) { return record.deferred == deferred.id; }))
        {
            continue;
        }
        const auto owner = GameplayObjectRef{effects::EffectService::Domain(), deferred.id.value};
        const auto persistence = deferred.persistence == effects::DeferredEffectPersistence::Persistent
                                     ? time::SchedulePersistence::Persistent
                                     : time::SchedulePersistence::Session;
        auto bound = deferred.schedule;

        auto pending_matches = [&](ScheduleId schedule) {
            return std::any_of(pending.pending.begin(), pending.pending.end(), [&](const auto& delivery) {
                return delivery.trigger.schedule == schedule && delivery.trigger.owner == owner &&
                       delivery.trigger.action == deferred_effect_action_;
            });
        };
        if (bound.has_value())
        {
            const auto entry = time_.GetSchedule(*bound);
            const bool valid_time = entry.has_value() &&
                                    ScheduleMatches(*entry, deferred.clock, owner, deferred_effect_action_, persistence) &&
                                    entry->due == deferred.due && entry->recurrence.kind == time::RecurrenceKind::Once;
            if (!valid_time && !pending_matches(*bound))
            {
                if (entry.has_value())
                {
                    const auto cancelled = time_.Cancel(*bound);
                    if (!cancelled)
                        return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
                }
                const auto cleared = effects_.ClearDeferredSchedule(deferred.id);
                if (!cleared)
                    return foundation::Result<std::uint64_t>::Failure(cleared.GetError());
                bound.reset();
                ++reconciled;
            }
        }
        if (bound.has_value())
            continue;

        std::vector<ScheduleId> candidates;
        for (const auto& entry : schedules)
        {
            if (time_.HasSchedule(entry.id) &&
                ScheduleMatches(entry, deferred.clock, owner, deferred_effect_action_, persistence) &&
                entry.due == deferred.due && entry.recurrence.kind == time::RecurrenceKind::Once)
                candidates.push_back(entry.id);
        }
        for (const auto& delivery : pending.pending)
        {
            if (delivery.trigger.owner == owner && delivery.trigger.action == deferred_effect_action_)
                candidates.push_back(delivery.trigger.schedule);
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        if (candidates.size() > 1)
        {
            for (std::size_t index = 1; index < candidates.size(); ++index)
            {
                if (time_.HasSchedule(candidates[index]))
                {
                    const auto cancelled = time_.Cancel(candidates[index]);
                    if (!cancelled)
                        return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
                    ++reconciled;
                }
                else
                {
                    return foundation::Result<std::uint64_t>::Failure(
                        Error("gameplay.deferred_effect_pending_duplicate",
                              "multiple pending dispatcher occurrences match one deferred effect"));
                }
            }
            candidates.resize(1);
        }

        ScheduleId schedule{};
        if (!candidates.empty())
        {
            schedule = candidates.front();
        }
        else
        {
            auto scheduled = time_.Schedule(deferred.clock, deferred.due, owner, deferred_effect_action_, {},
                                            time::CatchUpPolicy::FireOnce, persistence);
            if (!scheduled)
                return foundation::Result<std::uint64_t>::Failure(scheduled.GetError());
            schedule = scheduled.Value();
        }

        const auto bind = effects_.BindDeferredSchedule(deferred.id, schedule);
        if (!bind)
        {
            if (time_.HasSchedule(schedule))
                (void)time_.Cancel(schedule);
            return foundation::Result<std::uint64_t>::Failure(bind.GetError());
        }
        ++reconciled;
    }
    return foundation::Result<std::uint64_t>::Success(reconciled);
}

foundation::Result<std::uint64_t> StateTimeAdapter::SynchronizeDeferredEffects(GameplayContext context)
{
    std::uint64_t processed = 0;
    const auto batch = effects_.ReadChangesSince(effect_cursor_);
    if (batch.snapshot_required)
    {
        const auto reconciled = ReconcileDeferredEffectSchedules(context);
        if (!reconciled)
        {
            return foundation::Result<std::uint64_t>::Failure(reconciled.GetError());
        }
        effect_cursor_ = effects_.LatestChangeCursor();
        return foundation::Result<std::uint64_t>::Success(reconciled.Value());
    }

    ChangeCursor next_cursor = effect_cursor_;
    next_cursor.epoch = batch.latest_cursor.epoch;
    for (const auto& change : batch.changes)
    {
        if ((change.kind == effects::EffectChangeKind::DeferredCancelled || change.kind == effects::EffectChangeKind::DeferredExecuted) &&
            change.schedule.has_value() && time_.HasSchedule(*change.schedule))
        {
            const auto cancelled = time_.Cancel(*change.schedule);
            if (!cancelled)
            {
                return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
            }
        }
        next_cursor.sequence = change.sequence;
        ++processed;
    }
    effect_cursor_ = next_cursor;

    const auto reconciled = ReconcileDeferredEffectSchedules(context);
    if (!reconciled)
    {
        return foundation::Result<std::uint64_t>::Failure(reconciled.GetError());
    }
    return foundation::Result<std::uint64_t>::Success(processed + reconciled.Value());
}

bool StateTimeAdapter::HasPendingOccurrence(GameplayObjectRef owner, ActionTypeId action) const
{
    if (dispatcher_ == nullptr)
        return false;
    const auto snapshot = dispatcher_->CaptureSnapshot();
    return std::any_of(snapshot.pending.begin(), snapshot.pending.end(), [&](const auto& delivery) {
        return delivery.trigger.owner == owner && delivery.trigger.action == action;
    });
}

foundation::Result<integration::ScheduledTriggerDisposition> StateTimeAdapter::HandleExpirationTrigger(
    const time::ScheduledTrigger& trigger,
    const GameplayContext& context)
{
    if (trigger.owner.domain != conditions::ConditionService::Domain())
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);
    const conditions::ConditionInstanceId instance{trigger.owner.id};
    if (conditions_.Find(instance) == nullptr)
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);
    const auto handled = conditions_.HandleExpirationDue(instance, trigger.observed_at, context);
    if (!handled)
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::Retry);
    ++process_result_.condition_expirations;
    return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
        integration::ScheduledTriggerDisposition::Ack);
}

foundation::Result<integration::ScheduledTriggerDisposition> StateTimeAdapter::HandlePeriodicTrigger(
    const time::ScheduledTrigger& trigger,
    const GameplayContext& context)
{
    if (trigger.owner.domain != conditions::ConditionService::Domain())
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);
    const conditions::ConditionInstanceId instance{trigger.owner.id};
    if (conditions_.Find(instance) == nullptr)
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);
    const auto handled = conditions_.HandlePeriodicDue(instance, trigger.occurrence_count, context);
    if (!handled)
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::Retry);
    process_result_.condition_periodic += trigger.occurrence_count;
    return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
        integration::ScheduledTriggerDisposition::Ack);
}

foundation::Result<integration::ScheduledTriggerDisposition> StateTimeAdapter::HandleDeferredEffectTrigger(
    const time::ScheduledTrigger& trigger,
    const GameplayContext& context)
{
    if (trigger.owner.domain != effects::EffectService::Domain())
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);

    const effects::DeferredEffectId deferred{trigger.owner.id};
    const auto uncertain = std::find_if(deferred_reconciliations_.begin(), deferred_reconciliations_.end(),
                                        [&](const auto& record) { return record.schedule == trigger.schedule; });
    if (uncertain != deferred_reconciliations_.end())
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);

    auto request = effects_.PeekDeferredBySchedule(trigger.schedule, context);
    if (!request)
    {
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            effects_.FindDeferred(deferred) == nullptr
                ? integration::ScheduledTriggerDisposition::DiscardTerminal
                : integration::ScheduledTriggerDisposition::Retry);
    }

    if (deferred_reconciliations_.size() >= kDeferredReconciliationCapacity)
        return foundation::Result<integration::ScheduledTriggerDisposition>::Failure(
            Error("gameplay.deferred_effect_reconciliation_capacity",
                  "deferred effect reconciliation ledger is full"));
    deferred_reconciliations_.reserve(deferred_reconciliations_.size() + 1);

    auto effect_request = std::move(request).Value();
    effect_request.context = context;
    auto executed = effects_.Execute(std::move(effect_request), effect_budget_);
    if (!executed)
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::Retry);

    auto execution = std::move(executed).Value();
    process_result_.effect_executions.push_back(execution);
    if (execution.disposition == effects::EffectBatchDisposition::Succeeded)
    {
        const auto acknowledged = effects_.AcknowledgeDeferredBySchedule(trigger.schedule, context);
        if (acknowledged)
            return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
                integration::ScheduledTriggerDisposition::Ack);
    }
    else if (execution.disposition == effects::EffectBatchDisposition::Rejected)
    {
        const auto acknowledged = effects_.AcknowledgeDeferredBySchedule(trigger.schedule, context);
        if (acknowledged)
            return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
                integration::ScheduledTriggerDisposition::DiscardTerminal);
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::Retry);
    }

    const bool crossed_commit_boundary = std::any_of(
        execution.operations.begin(), execution.operations.end(), [](const auto& operation) {
            return operation.disposition == effects::EffectOperationDisposition::Applied ||
                   operation.disposition == effects::EffectOperationDisposition::NoOp;
        });
    if (execution.disposition == effects::EffectBatchDisposition::Failed && !crossed_commit_boundary)
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::Retry);

    deferred_reconciliations_.push_back(
        DeferredEffectReconciliationRecord{deferred, trigger.schedule, trigger, std::move(execution)});
    (void)effects_.AcknowledgeDeferredBySchedule(trigger.schedule, context);
    return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
        integration::ScheduledTriggerDisposition::DiscardTerminal);
}

foundation::Result<StateTimeProcessResult> StateTimeAdapter::ProcessDue(
    integration::ScheduledTriggerDispatcher& dispatcher,
    ClockId clock,
    GameplayContext context,
    time::SchedulerBudget scheduler_budget,
    effects::EffectExecutionBudget effect_budget)
{
    if (dispatcher_ != &dispatcher)
        return foundation::Result<StateTimeProcessResult>::Failure(
            Error("gameplay.state_time_dispatcher_missing", "state time adapter is not registered with this dispatcher"));

    const auto condition_sync = SynchronizeConditionSchedules(context);
    if (!condition_sync)
        return foundation::Result<StateTimeProcessResult>::Failure(condition_sync.GetError());
    const auto effect_sync = SynchronizeDeferredEffects(context);
    if (!effect_sync)
        return foundation::Result<StateTimeProcessResult>::Failure(effect_sync.GetError());

    effect_budget_ = effect_budget;
    process_result_ = {};
    const auto pumped = dispatcher.Pump(clock, context, scheduler_budget);
    if (!pumped)
        return foundation::Result<StateTimeProcessResult>::Failure(pumped.GetError());

    auto result = std::move(process_result_);
    process_result_ = {};
    return foundation::Result<StateTimeProcessResult>::Success(std::move(result));
}

StateTimeCheckpoint StateTimeAdapter::CaptureCheckpoint() const
{
    StateTimeCheckpoint checkpoint;
    checkpoint.condition_cursor = condition_cursor_;
    checkpoint.effect_cursor = effect_cursor_;
    checkpoint.condition_latest = conditions_.LatestChangeCursor();
    checkpoint.effect_latest = effects_.LatestChangeCursor();
    checkpoint.deferred_reconciliations = deferred_reconciliations_;
    std::sort(checkpoint.deferred_reconciliations.begin(), checkpoint.deferred_reconciliations.end(),
              [](const auto& left, const auto& right) { return left.deferred < right.deferred; });
    return checkpoint;
}

foundation::Result<void> StateTimeAdapter::ValidateCheckpoint(const StateTimeCheckpoint& checkpoint) const
{
    const auto current_condition = conditions_.LatestChangeCursor();
    if (checkpoint.schema_version != 3 || !checkpoint.condition_cursor.IsValid() || !checkpoint.effect_cursor.IsValid() ||
        checkpoint.condition_cursor.epoch != checkpoint.condition_latest.epoch ||
        checkpoint.effect_cursor.epoch != checkpoint.effect_latest.epoch ||
        checkpoint.condition_cursor.sequence > checkpoint.condition_latest.sequence ||
        checkpoint.effect_cursor.sequence > checkpoint.effect_latest.sequence ||
        checkpoint.condition_latest.sequence > current_condition.sequence ||
        checkpoint.deferred_reconciliations.size() > kDeferredReconciliationCapacity)
        return foundation::Result<void>::Failure(
            Error("gameplay.state_time_checkpoint_invalid", "state time checkpoint is invalid"));

    for (std::size_t index = 0; index < checkpoint.deferred_reconciliations.size(); ++index)
    {
        const auto& record = checkpoint.deferred_reconciliations[index];
        if (!record.deferred.IsValid() || !record.schedule.IsValid() || !record.trigger.schedule.IsValid() ||
            record.trigger.schedule != record.schedule || !record.execution.execution.IsValid() ||
            (index != 0 && checkpoint.deferred_reconciliations[index - 1].deferred == record.deferred))
            return foundation::Result<void>::Failure(
                Error("gameplay.state_time_checkpoint_invalid",
                      "state time checkpoint contains an invalid deferred reconciliation"));
    }
    return foundation::Result<void>::Success();
}

void StateTimeAdapter::ApplyCheckpoint(StateTimeCheckpoint checkpoint) noexcept
{
    const auto condition_latest = conditions_.LatestChangeCursor();
    const auto effect_latest = effects_.LatestChangeCursor();
    condition_cursor_ = condition_latest.sequence >= checkpoint.condition_latest.sequence
                            ? ChangeCursor{condition_latest.epoch, checkpoint.condition_cursor.sequence}
                            : ChangeCursor{condition_latest.epoch, 0};
    effect_cursor_ = effect_latest.sequence >= checkpoint.effect_latest.sequence
                         ? ChangeCursor{effect_latest.epoch, checkpoint.effect_cursor.sequence}
                         : ChangeCursor{effect_latest.epoch, 0};
    deferred_reconciliations_ = std::move(checkpoint.deferred_reconciliations);
}

foundation::Result<void> StateTimeAdapter::RestoreCheckpoint(StateTimeCheckpoint checkpoint)
{
    std::sort(checkpoint.deferred_reconciliations.begin(), checkpoint.deferred_reconciliations.end(),
              [](const auto& left, const auto& right) { return left.deferred < right.deferred; });
    const auto valid = ValidateCheckpoint(checkpoint);
    if (!valid)
        return valid;
    ApplyCheckpoint(std::move(checkpoint));
    return foundation::Result<void>::Success();
}

foundation::Result<void> StateTimeAdapter::ResolveDeferredReconciliation(effects::DeferredEffectId deferred)
{
    const auto found = std::find_if(deferred_reconciliations_.begin(), deferred_reconciliations_.end(),
                                    [&](const auto& record) { return record.deferred == deferred; });
    if (found == deferred_reconciliations_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.deferred_effect_reconciliation_missing", "deferred effect reconciliation record is missing"));

    if (effects_.FindDeferred(deferred) != nullptr)
    {
        const auto cancelled = effects_.CancelDeferred(deferred);
        if (!cancelled)
            return cancelled;
    }
    deferred_reconciliations_.erase(found);
    return foundation::Result<void>::Success();
}

StateIntegrationCheckpoint StateIntegrationPersistence::CaptureCheckpoint() const
{
    StateIntegrationCheckpoint checkpoint;
    checkpoint.facts = facts_.CaptureCheckpoint();
    checkpoint.lifecycle = lifecycle_.CaptureCheckpoint();
    checkpoint.condition_effects = condition_effects_.CaptureCheckpoint();
    checkpoint.time = time_.CaptureCheckpoint();
    return checkpoint;
}

foundation::Result<void> StateIntegrationPersistence::RestoreCheckpoint(StateIntegrationCheckpoint checkpoint)
{
    if (checkpoint.schema_version != 1)
        return foundation::Result<void>::Failure(
            Error("gameplay.state_integration_checkpoint_invalid", "state integration checkpoint schema is incompatible"));

    std::sort(checkpoint.condition_effects.deliveries.begin(), checkpoint.condition_effects.deliveries.end(),
              [](const auto& left, const auto& right) { return left.key.condition_sequence < right.key.condition_sequence; });
    std::sort(checkpoint.time.deferred_reconciliations.begin(), checkpoint.time.deferred_reconciliations.end(),
              [](const auto& left, const auto& right) { return left.deferred < right.deferred; });

    const auto facts_valid = facts_.ValidateCheckpoint(checkpoint.facts);
    if (!facts_valid)
        return facts_valid;
    const auto lifecycle_valid = lifecycle_.ValidateCheckpoint(checkpoint.lifecycle);
    if (!lifecycle_valid)
        return lifecycle_valid;
    const auto condition_effects_valid = condition_effects_.ValidateCheckpoint(checkpoint.condition_effects);
    if (!condition_effects_valid)
        return condition_effects_valid;
    const auto time_valid = time_.ValidateCheckpoint(checkpoint.time);
    if (!time_valid)
        return time_valid;

    facts_.ApplyCheckpoint(std::move(checkpoint.facts));
    lifecycle_.ApplyCheckpoint(std::move(checkpoint.lifecycle));
    condition_effects_.ApplyCheckpoint(std::move(checkpoint.condition_effects));
    time_.ApplyCheckpoint(std::move(checkpoint.time));
    return foundation::Result<void>::Success();
}

} // namespace epidemic::gameplay::state_integration







