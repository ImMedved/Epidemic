#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::materials
{
constexpr std::int64_t kFixedOne = 1'000'000;
constexpr std::uint32_t kCompositionOnePpm = 1'000'000;

struct MaterialId
{
    TypeId value{};
    [[nodiscard]] static constexpr MaterialId FromString(std::string_view name) noexcept { return MaterialId{TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr bool operator==(const MaterialId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const MaterialId&) const noexcept = default;
};

struct SubstanceId
{
    TypeId value{};
    [[nodiscard]] static constexpr SubstanceId FromString(std::string_view name) noexcept { return SubstanceId{TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr bool operator==(const SubstanceId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SubstanceId&) const noexcept = default;
};

struct MaterialSlotId
{
    TypeId value{};
    [[nodiscard]] static constexpr MaterialSlotId FromString(std::string_view name) noexcept { return MaterialSlotId{TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr bool operator==(const MaterialSlotId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const MaterialSlotId&) const noexcept = default;
};

struct MaterialReactionId
{
    TypeId value{};
    [[nodiscard]] static constexpr MaterialReactionId FromString(std::string_view name) noexcept { return MaterialReactionId{TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr bool operator==(const MaterialReactionId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const MaterialReactionId&) const noexcept = default;
};

struct MaterialResponseTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr MaterialResponseTypeId FromString(std::string_view name) noexcept { return MaterialResponseTypeId{TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr bool operator==(const MaterialResponseTypeId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const MaterialResponseTypeId&) const noexcept = default;
};

struct TypeHash
{
    template <typename T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept
    {
        return std::hash<TypeId>{}(id.value);
    }
};

struct MaterialPhysicalProfile
{
    std::int64_t density_micro = 0;
    std::int64_t hardness_micro = 0;
    std::int64_t toughness_micro = 0;
    std::int64_t brittleness_micro = 0;
};

struct MaterialThermalProfile
{
    std::int64_t flammability_micro = 0;
    std::int64_t ignition_threshold_micro = 0;
    std::int64_t burn_rate_micro = 0;
    std::int64_t thermal_capacity_micro = 0;
    std::int64_t thermal_conductivity_micro = 0;
};

struct MaterialChemicalProfile
{
    std::int64_t electrical_conductivity_micro = 0;
    std::int64_t porosity_micro = 0;
    std::int64_t absorption_micro = 0;
    std::int64_t corrosion_resistance_micro = 0;
    std::int64_t toxicity_micro = 0;
};

struct MaterialDefinition
{
    MaterialId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    MaterialPhysicalProfile physical{};
    MaterialThermalProfile thermal{};
    MaterialChemicalProfile chemical{};
    Revision revision{};
};

enum class SubstancePhase
{
    Solid,
    Liquid,
    Gas,
    Powder,
};

struct SubstanceDefinition
{
    SubstanceId id{};
    std::string canonical_name;
    SubstancePhase phase = SubstancePhase::Solid;
    GameplayTagSet tags;
    std::int64_t density_micro = 0;
    std::int64_t viscosity_micro = 0;
    std::int64_t flammability_micro = 0;
    std::int64_t volatility_micro = 0;
    std::int64_t toxicity_micro = 0;
    std::int64_t corrosiveness_micro = 0;
    std::int64_t conductivity_micro = 0;
    std::int64_t freeze_point_micro = 0;
    std::int64_t boiling_point_micro = 0;
};

struct MaterialConstituent
{
    MaterialId material{};
    std::uint32_t weight_ppm = 0;

    [[nodiscard]] constexpr bool operator==(const MaterialConstituent&) const noexcept = default;
};

struct MaterialComposition
{
    std::vector<MaterialConstituent> constituents;
};

struct SubstanceAmount
{
    std::int64_t micro = 0;

    constexpr SubstanceAmount() noexcept = default;
    constexpr SubstanceAmount(std::int64_t value_micro) noexcept : micro(value_micro) {}

    [[nodiscard]] constexpr bool IsPositive() const noexcept { return micro > 0; }
    [[nodiscard]] constexpr bool operator==(const SubstanceAmount&) const noexcept = default;
};

struct SubstanceQuantity
{
    SubstanceId substance{};
    SubstanceAmount amount{};

    [[nodiscard]] constexpr bool operator==(const SubstanceQuantity&) const noexcept = default;
};

struct SubstanceExposure
{
    SubstanceId substance{};
    SubstanceAmount amount{};
    std::uint32_t coverage_ppm = 0;

    [[nodiscard]] constexpr bool operator==(const SubstanceExposure&) const noexcept = default;
};

struct DynamicMaterialState
{
    std::int64_t temperature_micro = 0;
    std::uint32_t saturation_ppm = 0;
    std::uint32_t corrosion_ppm = 0;
    std::vector<SubstanceExposure> exposures;
    std::vector<SubstanceQuantity> contents;

    [[nodiscard]] constexpr bool operator==(const DynamicMaterialState&) const noexcept = default;
};

struct MaterialSlotKey
{
    GameplayObjectRef subject{};
    MaterialSlotId slot{};
    [[nodiscard]] constexpr bool operator==(const MaterialSlotKey&) const noexcept = default;
};

struct MaterialSlotKeyHash
{
    [[nodiscard]] std::size_t operator()(const MaterialSlotKey& key) const noexcept
    {
        auto seed = std::hash<GameplayObjectRef>{}(key.subject);
        const auto slot = std::hash<TypeId>{}(key.slot.value);
        seed ^= slot + 0x9E3779B97F4A7C15ull + (seed << 6u) + (seed >> 2u);
        return seed;
    }
};

struct MaterialSlotState
{
    MaterialSlotKey key{};
    MaterialComposition composition;
    DynamicMaterialState dynamic{};
    Revision revision{};
};

enum class MaterialStimulusType
{
    Heat,
    Cold,
    Electricity,
    Moisture,
    Drying,
    Corrosive,
    Chemical,
};

struct MaterialStimulus
{
    GameplayObjectRef target{};
    MaterialSlotId slot{};
    MaterialStimulusType type = MaterialStimulusType::Heat;
    std::int64_t magnitude_micro = 0;
    GameplayContext context{};
};

enum class ReactionThresholdField
{
    StimulusMagnitude,
    Temperature,
    Saturation,
    Corrosion,
    ExposureAmount,
};

struct MaterialReactionRule
{
    MaterialReactionId id{};
    std::string canonical_name;
    MaterialStimulusType stimulus = MaterialStimulusType::Heat;
    std::optional<TagId> required_material_tag{};
    std::optional<TagId> required_substance_tag{};
    ReactionThresholdField threshold_field = ReactionThresholdField::StimulusMagnitude;
    std::int64_t min_threshold_micro = 0;
    MaterialResponseTypeId response_type{};
    std::int64_t response_magnitude_micro = 0;
    int priority = 0;
};

struct MaterialResponse
{
    MaterialResponseTypeId type{};
    MaterialReactionId reaction{};
    GameplayObjectRef subject{};
    MaterialSlotId slot{};
    std::int64_t magnitude_micro = 0;
    GameplayContext context{};
};

enum class MaterialChangeKind
{
    Assigned,
    CompositionChanged,
    StateChanged,
    ExposureAdded,
    ExposureRemoved,
    ReactionTriggered,
    Removed,
};

struct MaterialChange
{
    std::uint64_t sequence = 0;
    MaterialChangeKind kind = MaterialChangeKind::Assigned;
    MaterialSlotKey key{};
    SubstanceId substance{};
    MaterialResponseTypeId response{};
    MaterialReactionId reaction{};
    Revision revision{};
    GameplayContext context{};
};

struct MaterialsSnapshot
{
    std::vector<MaterialSlotState> states;
    Revision revision{};
};

struct MaterialsDiagnostics
{
    std::uint64_t material_states = 0;
    std::uint64_t substance_exposures = 0;
    std::uint64_t stimuli = 0;
    std::uint64_t reaction_evaluations = 0;
    std::uint64_t reactions_triggered = 0;
};

class MaterialService
{
  public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.materials");
    }

    [[nodiscard]] foundation::Result<MaterialId> RegisterMaterial(MaterialDefinition definition);
    [[nodiscard]] foundation::Result<SubstanceId> RegisterSubstance(SubstanceDefinition definition);
    [[nodiscard]] foundation::Result<MaterialSlotId> RegisterSlot(std::string_view canonical_name);
    [[nodiscard]] foundation::Result<MaterialReactionId> RegisterReaction(MaterialReactionRule rule);
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] const MaterialDefinition* FindMaterial(MaterialId id) const noexcept;
    [[nodiscard]] const SubstanceDefinition* FindSubstance(SubstanceId id) const noexcept;
    [[nodiscard]] bool IsSlotRegistered(MaterialSlotId id) const noexcept { return slots_.contains(id); }

    [[nodiscard]] foundation::Result<void> AssignComposition(
        GameplayObjectRef subject,
        MaterialSlotId slot,
        MaterialComposition composition,
        GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveSlot(GameplayObjectRef subject, MaterialSlotId slot, GameplayContext context = {});
    [[nodiscard]] std::uint64_t RemoveSubject(GameplayObjectRef subject, GameplayContext context = {});

    [[nodiscard]] foundation::Result<void> ApplySubstanceExposure(
        GameplayObjectRef subject,
        MaterialSlotId slot,
        SubstanceId substance,
        SubstanceAmount amount,
        std::uint32_t coverage_ppm,
        GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SetContainedSubstance(
        GameplayObjectRef subject,
        MaterialSlotId slot,
        SubstanceId substance,
        SubstanceAmount amount,
        GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveContainedSubstance(
        GameplayObjectRef subject,
        MaterialSlotId slot,
        SubstanceId substance,
        GameplayContext context = {});    [[nodiscard]] foundation::Result<void> RemoveSubstanceExposure(
        GameplayObjectRef subject,
        MaterialSlotId slot,
        SubstanceId substance,
        GameplayContext context = {});

    [[nodiscard]] foundation::Result<std::vector<MaterialResponse>> EvaluateStimulus(
        const MaterialStimulus& stimulus,
        const GameplayTagRegistry& tags) const;
    [[nodiscard]] foundation::Result<std::vector<MaterialResponse>> ApplyStimulus(
        const MaterialStimulus& stimulus,
        const GameplayTagRegistry& tags);

    [[nodiscard]] std::optional<MaterialSlotState> FindState(GameplayObjectRef subject, MaterialSlotId slot) const;
    [[nodiscard]] std::vector<MaterialSlotState> FindSlots(GameplayObjectRef subject) const;
    [[nodiscard]] std::vector<MaterialSlotState> FindExposedTo(SubstanceId substance) const;
    [[nodiscard]] std::vector<MaterialSlotState> FindByMaterialTag(TagId tag, const GameplayTagRegistry& tags) const;

    [[nodiscard]] std::vector<MaterialChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept { return next_change_sequence_ - 1; }
    void PruneChangesBefore(std::uint64_t sequence);

    [[nodiscard]] MaterialsSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(MaterialsSnapshot snapshot);
    [[nodiscard]] MaterialsDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    [[nodiscard]] foundation::Result<void> ValidateComposition(MaterialComposition& composition) const;
    [[nodiscard]] bool HasMaterialTag(const MaterialSlotState& state, TagId required, const GameplayTagRegistry& tags) const;
    [[nodiscard]] bool HasSubstanceTag(const MaterialSlotState& state, TagId required, const GameplayTagRegistry& tags) const;
    [[nodiscard]] std::int64_t ThresholdValue(const MaterialSlotState& state, const MaterialStimulus& stimulus, const MaterialReactionRule& rule, const GameplayTagRegistry& tags) const noexcept;
    [[nodiscard]] std::vector<MaterialResponse> EvaluateRules(
        const MaterialSlotState& state,
        const MaterialStimulus& stimulus,
        const GameplayTagRegistry& tags) const;
    void RecordChange(MaterialChange change);
    void BumpRevision(MaterialSlotState& state) noexcept;

    std::unordered_map<MaterialId, MaterialDefinition, TypeHash> materials_;
    std::unordered_map<SubstanceId, SubstanceDefinition, TypeHash> substances_;
    std::unordered_map<MaterialSlotId, std::string, TypeHash> slots_;
    std::unordered_map<MaterialReactionId, MaterialReactionRule, TypeHash> reactions_;
    std::vector<MaterialReactionId> reaction_order_;
    std::unordered_map<MaterialSlotKey, MaterialSlotState, MaterialSlotKeyHash> states_;

    Revision revision_{};
    bool frozen_ = false;
    std::vector<MaterialChange> changes_;
    std::uint64_t next_change_sequence_ = 1;

    mutable std::uint64_t reaction_evaluations_ = 0;
    std::uint64_t stimuli_ = 0;
    std::uint64_t reactions_triggered_ = 0;
};
} // namespace epidemic::gameplay::materials




