#include "Epidemic/GameFramework/Materials/materials.h"

#include "Epidemic/Foundation/error.h"

#include <limits>

namespace epidemic::gameplay::materials
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}

[[nodiscard]] std::uint32_t ClampPpm(std::int64_t value) noexcept
{
    if (value <= 0)
    {
        return 0;
    }
    if (value >= static_cast<std::int64_t>(kCompositionOnePpm))
    {
        return kCompositionOnePpm;
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int64_t SaturatingAdd(std::int64_t left, std::int64_t right) noexcept
{
    if (right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
    {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)
    {
        return std::numeric_limits<std::int64_t>::min();
    }
    return left + right;
}
} // namespace

foundation::Result<MaterialId> MaterialService::RegisterMaterial(MaterialDefinition definition)
{
    if (frozen_)
    {
        return foundation::Result<MaterialId>::Failure(Error("gameplay.registry_frozen", "material registry is frozen"));
    }
    if (definition.canonical_name.empty())
    {
        return foundation::Result<MaterialId>::Failure(Error("gameplay.material_invalid", "material canonical name must not be empty"));
    }
    const auto expected = MaterialId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
    {
        definition.id = expected;
    }
    if (definition.id != expected)
    {
        return foundation::Result<MaterialId>::Failure(Error("gameplay.material_id_mismatch", "material id does not match canonical name"));
    }
    if (definition.physical.density_micro < 0 || definition.physical.hardness_micro < 0 ||
        definition.physical.toughness_micro < 0 || definition.physical.brittleness_micro < 0 ||
        definition.thermal.flammability_micro < 0 || definition.thermal.burn_rate_micro < 0 ||
        definition.thermal.thermal_capacity_micro < 0 || definition.thermal.thermal_conductivity_micro < 0 ||
        definition.chemical.electrical_conductivity_micro < 0 || definition.chemical.porosity_micro < 0 ||
        definition.chemical.absorption_micro < 0 || definition.chemical.corrosion_resistance_micro < 0 ||
        definition.chemical.toxicity_micro < 0)
    {
        return foundation::Result<MaterialId>::Failure(Error("gameplay.material_invalid", "material non-negative properties must not be negative"));
    }
    if (materials_.contains(definition.id))
    {
        return foundation::Result<MaterialId>::Failure(Error("gameplay.already_registered", "material is already registered"));
    }
    definition.revision = Revision{1};
    const auto id = definition.id;
    materials_.emplace(id, std::move(definition));
    return foundation::Result<MaterialId>::Success(id);
}

foundation::Result<SubstanceId> MaterialService::RegisterSubstance(SubstanceDefinition definition)
{
    if (frozen_)
    {
        return foundation::Result<SubstanceId>::Failure(Error("gameplay.registry_frozen", "substance registry is frozen"));
    }
    if (definition.canonical_name.empty())
    {
        return foundation::Result<SubstanceId>::Failure(Error("gameplay.substance_invalid", "substance canonical name must not be empty"));
    }
    const auto expected = SubstanceId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
    {
        definition.id = expected;
    }
    if (definition.id != expected)
    {
        return foundation::Result<SubstanceId>::Failure(Error("gameplay.substance_id_mismatch", "substance id does not match canonical name"));
    }
    if (definition.density_micro < 0 || definition.viscosity_micro < 0 || definition.flammability_micro < 0 ||
        definition.volatility_micro < 0 || definition.toxicity_micro < 0 || definition.corrosiveness_micro < 0 ||
        definition.conductivity_micro < 0 || definition.freeze_point_micro > definition.boiling_point_micro)
    {
        return foundation::Result<SubstanceId>::Failure(Error("gameplay.substance_invalid", "substance properties are invalid"));
    }
    if (substances_.contains(definition.id))
    {
        return foundation::Result<SubstanceId>::Failure(Error("gameplay.already_registered", "substance is already registered"));
    }
    const auto id = definition.id;
    substances_.emplace(id, std::move(definition));
    return foundation::Result<SubstanceId>::Success(id);
}

foundation::Result<MaterialSlotId> MaterialService::RegisterSlot(std::string_view canonical_name)
{
    if (frozen_)
    {
        return foundation::Result<MaterialSlotId>::Failure(Error("gameplay.registry_frozen", "material slot registry is frozen"));
    }
    if (canonical_name.empty())
    {
        return foundation::Result<MaterialSlotId>::Failure(Error("gameplay.material_slot_invalid", "material slot name must not be empty"));
    }
    const auto id = MaterialSlotId::FromString(canonical_name);
    if (slots_.contains(id))
    {
        return foundation::Result<MaterialSlotId>::Failure(Error("gameplay.already_registered", "material slot is already registered"));
    }
    slots_.emplace(id, std::string(canonical_name));
    return foundation::Result<MaterialSlotId>::Success(id);
}

foundation::Result<MaterialReactionId> MaterialService::RegisterReaction(MaterialReactionRule rule)
{
    if (frozen_)
    {
        return foundation::Result<MaterialReactionId>::Failure(Error("gameplay.registry_frozen", "material reaction registry is frozen"));
    }
    if (rule.canonical_name.empty() || !rule.response_type.IsValid())
    {
        return foundation::Result<MaterialReactionId>::Failure(Error("gameplay.material_reaction_invalid", "reaction name and response type must be valid"));
    }
    const auto expected = MaterialReactionId::FromString(rule.canonical_name);
    if (!rule.id.IsValid())
    {
        rule.id = expected;
    }
    if (rule.id != expected)
    {
        return foundation::Result<MaterialReactionId>::Failure(Error("gameplay.material_reaction_id_mismatch", "reaction id does not match canonical name"));
    }
    if (reactions_.contains(rule.id))
    {
        return foundation::Result<MaterialReactionId>::Failure(Error("gameplay.already_registered", "material reaction is already registered"));
    }
    const auto id = rule.id;
    reactions_.emplace(id, std::move(rule));
    reaction_order_.push_back(id);
    std::sort(reaction_order_.begin(), reaction_order_.end(), [this](MaterialReactionId left, MaterialReactionId right) {
        const auto& a = reactions_.at(left);
        const auto& b = reactions_.at(right);
        if (a.priority != b.priority)
        {
            return a.priority < b.priority;
        }
        return a.id < b.id;
    });
    return foundation::Result<MaterialReactionId>::Success(id);
}

const MaterialDefinition* MaterialService::FindMaterial(MaterialId id) const noexcept
{
    const auto found = materials_.find(id);
    return found == materials_.end() ? nullptr : &found->second;
}

const SubstanceDefinition* MaterialService::FindSubstance(SubstanceId id) const noexcept
{
    const auto found = substances_.find(id);
    return found == substances_.end() ? nullptr : &found->second;
}

foundation::Result<void> MaterialService::ValidateComposition(MaterialComposition& composition) const
{
    if (composition.constituents.empty())
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_composition_empty", "material composition must contain at least one constituent"));
    }
    std::sort(composition.constituents.begin(), composition.constituents.end(), [](const auto& left, const auto& right) {
        return left.material < right.material;
    });
    std::uint64_t total = 0;
    MaterialId previous{};
    for (const auto& constituent : composition.constituents)
    {
        if (!constituent.material.IsValid() || FindMaterial(constituent.material) == nullptr || constituent.weight_ppm == 0)
        {
            return foundation::Result<void>::Failure(Error("gameplay.material_composition_invalid", "material composition references invalid material or zero weight"));
        }
        if (previous.IsValid() && previous == constituent.material)
        {
            return foundation::Result<void>::Failure(Error("gameplay.material_composition_duplicate", "material composition contains duplicate material"));
        }
        previous = constituent.material;
        total += constituent.weight_ppm;
    }
    if (total != kCompositionOnePpm)
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_composition_weight", "material composition weights must sum to 1,000,000 ppm"));
    }
    return foundation::Result<void>::Success();
}

void MaterialService::BumpRevision(MaterialSlotState& state) noexcept
{
    if (revision_.value < std::numeric_limits<std::uint64_t>::max())
    {
        ++revision_.value;
    }
    state.revision = revision_;
}

foundation::Result<void> MaterialService::AssignComposition(
    GameplayObjectRef subject,
    MaterialSlotId slot,
    MaterialComposition composition,
    GameplayContext context)
{
    if (!subject.IsValid() || !IsSlotRegistered(slot))
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_assignment_invalid", "subject and registered material slot are required"));
    }
    auto validation = ValidateComposition(composition);
    if (!validation)
    {
        return validation;
    }

    const MaterialSlotKey key{subject, slot};
    auto found = states_.find(key);
    const auto kind = found == states_.end() ? MaterialChangeKind::Assigned : MaterialChangeKind::CompositionChanged;
    if (found == states_.end())
    {
        MaterialSlotState state;
        state.key = key;
        state.composition = std::move(composition);
        BumpRevision(state);
        found = states_.emplace(key, std::move(state)).first;
    }
    else
    {
        if (found->second.composition.constituents == composition.constituents)
        {
            return foundation::Result<void>::Success();
        }
        found->second.composition = std::move(composition);
        BumpRevision(found->second);
    }
    RecordChange(MaterialChange{0, kind, key, {}, {}, {}, found->second.revision, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> MaterialService::RemoveSlot(GameplayObjectRef subject, MaterialSlotId slot, GameplayContext context)
{
    const MaterialSlotKey key{subject, slot};
    const auto found = states_.find(key);
    if (found == states_.end())
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_state_unknown", "material slot state does not exist"));
    }
    ++revision_.value;
    RecordChange(MaterialChange{0, MaterialChangeKind::Removed, key, {}, {}, {}, revision_, context});
    states_.erase(found);
    return foundation::Result<void>::Success();
}

std::uint64_t MaterialService::RemoveSubject(GameplayObjectRef subject, GameplayContext context)
{
    std::vector<MaterialSlotKey> keys;
    for (const auto& [key, _] : states_)
    {
        if (key.subject == subject)
        {
            keys.push_back(key);
        }
    }
    std::sort(keys.begin(), keys.end(), [](const auto& left, const auto& right) { return left.slot < right.slot; });
    for (const auto& key : keys)
    {
        [[maybe_unused]] const auto result = RemoveSlot(key.subject, key.slot, context);
    }
    return keys.size();
}

foundation::Result<void> MaterialService::ApplySubstanceExposure(
    GameplayObjectRef subject,
    MaterialSlotId slot,
    SubstanceId substance,
    SubstanceAmount amount,
    std::uint32_t coverage_ppm,
    GameplayContext context)
{
    if (FindSubstance(substance) == nullptr || !amount.IsPositive() || coverage_ppm == 0 || coverage_ppm > kCompositionOnePpm)
    {
        return foundation::Result<void>::Failure(Error("gameplay.substance_exposure_invalid", "substance exposure parameters are invalid"));
    }
    auto found = states_.find(MaterialSlotKey{subject, slot});
    if (found == states_.end())
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_state_unknown", "material slot state does not exist"));
    }
    auto& exposures = found->second.dynamic.exposures;
    auto exposure = std::lower_bound(exposures.begin(), exposures.end(), substance, [](const SubstanceExposure& item, SubstanceId id) {
        return item.substance < id;
    });
    if (exposure == exposures.end() || exposure->substance != substance)
    {
        exposure = exposures.insert(exposure, SubstanceExposure{substance, amount, coverage_ppm});
    }
    else
    {
        exposure->amount.micro = SaturatingAdd(exposure->amount.micro, amount.micro);
        exposure->coverage_ppm = std::max(exposure->coverage_ppm, coverage_ppm);
    }
    BumpRevision(found->second);
    RecordChange(MaterialChange{0, MaterialChangeKind::ExposureAdded, found->first, substance, {}, {}, found->second.revision, context});
    return foundation::Result<void>::Success();
}


foundation::Result<void> MaterialService::SetContainedSubstance(
    GameplayObjectRef subject,
    MaterialSlotId slot,
    SubstanceId substance,
    SubstanceAmount amount,
    GameplayContext context)
{
    if (FindSubstance(substance) == nullptr || !amount.IsPositive())
    {
        return foundation::Result<void>::Failure(Error("gameplay.substance_quantity_invalid", "contained substance quantity is invalid"));
    }
    auto found = states_.find(MaterialSlotKey{subject, slot});
    if (found == states_.end())
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_state_unknown", "material slot state does not exist"));
    }
    auto& contents = found->second.dynamic.contents;
    auto quantity = std::lower_bound(contents.begin(), contents.end(), substance, [](const SubstanceQuantity& item, SubstanceId id) {
        return item.substance < id;
    });
    if (quantity == contents.end() || quantity->substance != substance)
    {
        contents.insert(quantity, SubstanceQuantity{substance, amount});
    }
    else
    {
        if (quantity->amount == amount)
        {
            return foundation::Result<void>::Success();
        }
        quantity->amount = amount;
    }
    BumpRevision(found->second);
    RecordChange(MaterialChange{0, MaterialChangeKind::StateChanged, found->first, substance, {}, {}, found->second.revision, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> MaterialService::RemoveContainedSubstance(
    GameplayObjectRef subject,
    MaterialSlotId slot,
    SubstanceId substance,
    GameplayContext context)
{
    auto found = states_.find(MaterialSlotKey{subject, slot});
    if (found == states_.end())
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_state_unknown", "material slot state does not exist"));
    }
    auto& contents = found->second.dynamic.contents;
    const auto quantity = std::lower_bound(contents.begin(), contents.end(), substance, [](const SubstanceQuantity& item, SubstanceId id) {
        return item.substance < id;
    });
    if (quantity == contents.end() || quantity->substance != substance)
    {
        return foundation::Result<void>::Failure(Error("gameplay.substance_quantity_unknown", "contained substance quantity does not exist"));
    }
    contents.erase(quantity);
    BumpRevision(found->second);
    RecordChange(MaterialChange{0, MaterialChangeKind::StateChanged, found->first, substance, {}, {}, found->second.revision, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> MaterialService::RemoveSubstanceExposure(
    GameplayObjectRef subject,
    MaterialSlotId slot,
    SubstanceId substance,
    GameplayContext context)
{
    auto found = states_.find(MaterialSlotKey{subject, slot});
    if (found == states_.end())
    {
        return foundation::Result<void>::Failure(Error("gameplay.material_state_unknown", "material slot state does not exist"));
    }
    auto& exposures = found->second.dynamic.exposures;
    const auto exposure = std::lower_bound(exposures.begin(), exposures.end(), substance, [](const SubstanceExposure& item, SubstanceId id) {
        return item.substance < id;
    });
    if (exposure == exposures.end() || exposure->substance != substance)
    {
        return foundation::Result<void>::Failure(Error("gameplay.substance_exposure_unknown", "substance exposure does not exist"));
    }
    exposures.erase(exposure);
    BumpRevision(found->second);
    RecordChange(MaterialChange{0, MaterialChangeKind::ExposureRemoved, found->first, substance, {}, {}, found->second.revision, context});
    return foundation::Result<void>::Success();
}

bool MaterialService::HasMaterialTag(const MaterialSlotState& state, TagId required, const GameplayTagRegistry& tags) const
{
    for (const auto& constituent : state.composition.constituents)
    {
        const auto* definition = FindMaterial(constituent.material);
        if (definition != nullptr && definition->tags.HasMatching(required, tags))
        {
            return true;
        }
    }
    return false;
}

bool MaterialService::HasSubstanceTag(const MaterialSlotState& state, TagId required, const GameplayTagRegistry& tags) const
{
    for (const auto& exposure : state.dynamic.exposures)
    {
        const auto* definition = FindSubstance(exposure.substance);
        if (definition != nullptr && definition->tags.HasMatching(required, tags))
        {
            return true;
        }
    }
    return false;
}

std::int64_t MaterialService::ThresholdValue(
    const MaterialSlotState& state,
    const MaterialStimulus& stimulus,
    const MaterialReactionRule& rule,
    const GameplayTagRegistry& tags) const noexcept
{
    switch (rule.threshold_field)
    {
    case ReactionThresholdField::StimulusMagnitude:
        return stimulus.magnitude_micro;
    case ReactionThresholdField::Temperature:
        return state.dynamic.temperature_micro;
    case ReactionThresholdField::Saturation:
        return state.dynamic.saturation_ppm;
    case ReactionThresholdField::Corrosion:
        return state.dynamic.corrosion_ppm;
    case ReactionThresholdField::ExposureAmount:
    {
        std::int64_t amount = 0;
        for (const auto& exposure : state.dynamic.exposures)
        {
            if (!rule.required_substance_tag.has_value())
            {
                amount = SaturatingAdd(amount, exposure.amount.micro);
                continue;
            }
            const auto* definition = FindSubstance(exposure.substance);
            if (definition != nullptr && definition->tags.HasMatching(*rule.required_substance_tag, tags))
            {
                amount = SaturatingAdd(amount, exposure.amount.micro);
            }
        }
        return amount;
    }
    }
    return 0;
}

std::vector<MaterialResponse> MaterialService::EvaluateRules(
    const MaterialSlotState& state,
    const MaterialStimulus& stimulus,
    const GameplayTagRegistry& tags) const
{
    std::vector<MaterialResponse> result;
    for (const auto reaction_id : reaction_order_)
    {
        ++reaction_evaluations_;
        const auto& rule = reactions_.at(reaction_id);
        if (rule.stimulus != stimulus.type)
        {
            continue;
        }
        if (rule.required_material_tag.has_value() && !HasMaterialTag(state, *rule.required_material_tag, tags))
        {
            continue;
        }
        if (rule.required_substance_tag.has_value() && !HasSubstanceTag(state, *rule.required_substance_tag, tags))
        {
            continue;
        }
        if (ThresholdValue(state, stimulus, rule, tags) < rule.min_threshold_micro)
        {
            continue;
        }
        result.push_back(MaterialResponse{rule.response_type,
                                          rule.id,
                                          state.key.subject,
                                          state.key.slot,
                                          rule.response_magnitude_micro == 0 ? stimulus.magnitude_micro : rule.response_magnitude_micro,
                                          stimulus.context});
    }
    return result;
}

foundation::Result<std::vector<MaterialResponse>> MaterialService::EvaluateStimulus(
    const MaterialStimulus& stimulus,
    const GameplayTagRegistry& tags) const
{
    if (stimulus.magnitude_micro < 0)
    {
        return foundation::Result<std::vector<MaterialResponse>>::Failure(Error("gameplay.material_stimulus_invalid", "stimulus magnitude must be non-negative"));
    }
    const auto found = states_.find(MaterialSlotKey{stimulus.target, stimulus.slot});
    if (found == states_.end())
    {
        return foundation::Result<std::vector<MaterialResponse>>::Failure(Error("gameplay.material_state_unknown", "material slot state does not exist"));
    }
    return foundation::Result<std::vector<MaterialResponse>>::Success(EvaluateRules(found->second, stimulus, tags));
}

foundation::Result<std::vector<MaterialResponse>> MaterialService::ApplyStimulus(
    const MaterialStimulus& stimulus,
    const GameplayTagRegistry& tags)
{
    if (stimulus.magnitude_micro < 0)
    {
        return foundation::Result<std::vector<MaterialResponse>>::Failure(Error("gameplay.material_stimulus_invalid", "stimulus magnitude must be non-negative"));
    }
    auto found = states_.find(MaterialSlotKey{stimulus.target, stimulus.slot});
    if (found == states_.end())
    {
        return foundation::Result<std::vector<MaterialResponse>>::Failure(Error("gameplay.material_state_unknown", "material slot state does not exist"));
    }

    auto& state = found->second;
    const auto before = state.dynamic;
    switch (stimulus.type)
    {
    case MaterialStimulusType::Heat:
        state.dynamic.temperature_micro = SaturatingAdd(state.dynamic.temperature_micro, stimulus.magnitude_micro);
        break;
    case MaterialStimulusType::Cold:
        state.dynamic.temperature_micro = SaturatingAdd(state.dynamic.temperature_micro, -stimulus.magnitude_micro);
        break;
    case MaterialStimulusType::Moisture:
        state.dynamic.saturation_ppm = ClampPpm(SaturatingAdd(static_cast<std::int64_t>(state.dynamic.saturation_ppm), stimulus.magnitude_micro));
        break;
    case MaterialStimulusType::Drying:
        state.dynamic.saturation_ppm = ClampPpm(SaturatingAdd(static_cast<std::int64_t>(state.dynamic.saturation_ppm), -stimulus.magnitude_micro));
        break;
    case MaterialStimulusType::Corrosive:
        state.dynamic.corrosion_ppm = ClampPpm(SaturatingAdd(static_cast<std::int64_t>(state.dynamic.corrosion_ppm), stimulus.magnitude_micro));
        break;
    case MaterialStimulusType::Electricity:
    case MaterialStimulusType::Chemical:
        break;
    }
    ++stimuli_;
    if (!(state.dynamic == before))
    {
        BumpRevision(state);
        RecordChange(MaterialChange{0, MaterialChangeKind::StateChanged, state.key, {}, {}, {}, state.revision, stimulus.context});
    }

    auto responses = EvaluateRules(state, stimulus, tags);
    reactions_triggered_ += responses.size();
    for (const auto& response : responses)
    {
        RecordChange(MaterialChange{0,
                                    MaterialChangeKind::ReactionTriggered,
                                    state.key,
                                    {},
                                    response.type,
                                    response.reaction,
                                    state.revision,
                                    stimulus.context});
    }
    return foundation::Result<std::vector<MaterialResponse>>::Success(std::move(responses));
}

std::optional<MaterialSlotState> MaterialService::FindState(GameplayObjectRef subject, MaterialSlotId slot) const
{
    const auto found = states_.find(MaterialSlotKey{subject, slot});
    if (found == states_.end())
    {
        return std::nullopt;
    }
    return found->second;
}

std::vector<MaterialSlotState> MaterialService::FindSlots(GameplayObjectRef subject) const
{
    std::vector<MaterialSlotState> result;
    for (const auto& [key, state] : states_)
    {
        if (key.subject == subject)
        {
            result.push_back(state);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.key.slot < right.key.slot; });
    return result;
}

std::vector<MaterialSlotState> MaterialService::FindExposedTo(SubstanceId substance) const
{
    std::vector<MaterialSlotState> result;
    for (const auto& [_, state] : states_)
    {
        const auto found = std::lower_bound(state.dynamic.exposures.begin(), state.dynamic.exposures.end(), substance, [](const SubstanceExposure& exposure, SubstanceId id) {
            return exposure.substance < id;
        });
        if (found != state.dynamic.exposures.end() && found->substance == substance)
        {
            result.push_back(state);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.key.subject != right.key.subject)
        {
            return left.key.subject < right.key.subject;
        }
        return left.key.slot < right.key.slot;
    });
    return result;
}

std::vector<MaterialSlotState> MaterialService::FindByMaterialTag(TagId tag, const GameplayTagRegistry& tags) const
{
    std::vector<MaterialSlotState> result;
    for (const auto& [_, state] : states_)
    {
        if (HasMaterialTag(state, tag, tags))
        {
            result.push_back(state);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.key.subject != right.key.subject)
        {
            return left.key.subject < right.key.subject;
        }
        return left.key.slot < right.key.slot;
    });
    return result;
}

void MaterialService::RecordChange(MaterialChange change)
{
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ < std::numeric_limits<std::uint64_t>::max())
    {
        ++next_change_sequence_;
    }
    changes_.push_back(std::move(change));
}

std::vector<MaterialChange> MaterialService::ChangesSince(std::uint64_t sequence) const
{
    const auto found = std::upper_bound(changes_.begin(), changes_.end(), sequence, [](std::uint64_t value, const MaterialChange& change) {
        return value < change.sequence;
    });
    return std::vector<MaterialChange>(found, changes_.end());
}

void MaterialService::PruneChangesBefore(std::uint64_t sequence)
{
    const auto found = std::lower_bound(changes_.begin(), changes_.end(), sequence, [](const MaterialChange& change, std::uint64_t value) {
        return change.sequence < value;
    });
    changes_.erase(changes_.begin(), found);
}

MaterialsSnapshot MaterialService::CaptureSnapshot() const
{
    MaterialsSnapshot snapshot;
    snapshot.revision = revision_;
    snapshot.states.reserve(states_.size());
    for (const auto& [_, state] : states_)
    {
        snapshot.states.push_back(state);
    }
    std::sort(snapshot.states.begin(), snapshot.states.end(), [](const auto& left, const auto& right) {
        if (left.key.subject != right.key.subject)
        {
            return left.key.subject < right.key.subject;
        }
        return left.key.slot < right.key.slot;
    });
    return snapshot;
}

foundation::Result<void> MaterialService::RestoreSnapshot(MaterialsSnapshot snapshot)
{
    std::unordered_map<MaterialSlotKey, MaterialSlotState, MaterialSlotKeyHash> rebuilt;
    for (auto& state : snapshot.states)
    {
        auto composition = state.composition;
        auto validation = ValidateComposition(composition);
        if (!validation || !state.key.subject.IsValid() || !IsSlotRegistered(state.key.slot) || rebuilt.contains(state.key))
        {
            return foundation::Result<void>::Failure(Error("gameplay.material_snapshot_invalid", "material snapshot contains invalid state"));
        }
        if (state.dynamic.saturation_ppm > kCompositionOnePpm || state.dynamic.corrosion_ppm > kCompositionOnePpm ||
            snapshot.revision < state.revision)
        {
            return foundation::Result<void>::Failure(Error("gameplay.material_snapshot_invalid", "material snapshot contains invalid dynamic state"));
        }
        std::sort(state.dynamic.exposures.begin(), state.dynamic.exposures.end(), [](const auto& left, const auto& right) {
            return left.substance < right.substance;
        });
        SubstanceId previous_exposure{};
        for (const auto& exposure : state.dynamic.exposures)
        {
            if (FindSubstance(exposure.substance) == nullptr || !exposure.amount.IsPositive() || exposure.coverage_ppm == 0 || exposure.coverage_ppm > kCompositionOnePpm ||
                (previous_exposure.IsValid() && previous_exposure == exposure.substance))
            {
                return foundation::Result<void>::Failure(Error("gameplay.material_snapshot_invalid", "material snapshot contains invalid substance exposure"));
            }
            previous_exposure = exposure.substance;
        }
        std::sort(state.dynamic.contents.begin(), state.dynamic.contents.end(), [](const auto& left, const auto& right) {
            return left.substance < right.substance;
        });
        SubstanceId previous_quantity{};
        for (const auto& quantity : state.dynamic.contents)
        {
            if (FindSubstance(quantity.substance) == nullptr || !quantity.amount.IsPositive() ||
                (previous_quantity.IsValid() && previous_quantity == quantity.substance))
            {
                return foundation::Result<void>::Failure(Error("gameplay.material_snapshot_invalid", "material snapshot contains invalid contained substance"));
            }
            previous_quantity = quantity.substance;
        }
        state.composition = std::move(composition);
        rebuilt.emplace(state.key, std::move(state));
    }
    states_ = std::move(rebuilt);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    stimuli_ = 0;
    reactions_triggered_ = 0;
    reaction_evaluations_ = 0;
    return foundation::Result<void>::Success();
}

MaterialsDiagnostics MaterialService::GetDiagnostics() const noexcept
{
    MaterialsDiagnostics diagnostics;
    diagnostics.material_states = states_.size();
    diagnostics.stimuli = stimuli_;
    diagnostics.reaction_evaluations = reaction_evaluations_;
    diagnostics.reactions_triggered = reactions_triggered_;
    for (const auto& [_, state] : states_)
    {
        diagnostics.substance_exposures += state.dynamic.exposures.size();
    }
    return diagnostics;
}
} // namespace epidemic::gameplay::materials



