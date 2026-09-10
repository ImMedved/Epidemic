#include "Epidemic/GameFramework/Materials/materials.h"

#include <array>
#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::materials;

int main()
{
    GameplayTagRegistry tags;
    const auto wood_tag = tags.Register("material.wood");
    const auto liquid_tag = tags.Register("substance.liquid");
    if (!wood_tag || !liquid_tag) return 1;
    tags.Freeze();

    MaterialService service;
    MaterialDefinition wood;
    wood.canonical_name = "test.material.wood";
    wood.tags.Add(wood_tag.Value());
    wood.thermal.ignition_threshold_micro = 100;
    const auto wood_id = service.RegisterMaterial(wood);
    MaterialDefinition iron;
    iron.canonical_name = "test.material.iron";
    const auto iron_id = service.RegisterMaterial(iron);

    SubstanceDefinition water;
    water.canonical_name = "test.substance.water";
    water.phase = SubstancePhase::Liquid;
    water.tags.Add(liquid_tag.Value());
    const auto water_id = service.RegisterSubstance(water);

    const auto slot = service.RegisterSlot("test.slot.body");
    MaterialReactionRule ignition;
    ignition.canonical_name = "test.reaction.wood_heat";
    ignition.stimulus = MaterialStimulusType::Heat;
    ignition.required_material_tag = wood_tag.Value();
    ignition.threshold_field = ReactionThresholdField::Temperature;
    ignition.min_threshold_micro = 100;
    ignition.response_type = MaterialResponseTypeId::FromString("test.response.ignite");
    ignition.priority = 5;
    const auto reaction = service.RegisterReaction(ignition);
    if (!wood_id || !iron_id || !water_id || !slot || !reaction) return 2;
    const GameplayObjectRef pre_freeze_subject{GameplayDomainId::FromString("test.domain"), GameplayObjectId::FromString("test.pre_freeze")};
    MaterialComposition pre_freeze_composition{{MaterialConstituent{wood_id.Value(), 1'000'000}}};
    if (service.AssignComposition(pre_freeze_subject, slot.Value(), pre_freeze_composition)) return 22;
    service.Freeze();
    if (service.RegisterSlot("test.slot.late")) return 3;

    const GameplayObjectRef subject{GameplayDomainId::FromString("test.domain"), GameplayObjectId::FromString("test.object")};
    MaterialComposition bad{{MaterialConstituent{wood_id.Value(), 500000}}};
    if (service.AssignComposition(subject, slot.Value(), bad)) return 4;

    MaterialComposition composition{{MaterialConstituent{wood_id.Value(), 800000}, MaterialConstituent{iron_id.Value(), 200000}}};
    if (!service.AssignComposition(subject, slot.Value(), composition)) return 5;
    if (!service.ApplySubstanceExposure(subject, slot.Value(), water_id.Value(), 1000, 500000)) return 6;
    if (service.ApplySubstanceExposure(subject, slot.Value(), water_id.Value(), 1, 0)) return 16;
    if (!service.SetContainedSubstance(subject, slot.Value(), water_id.Value(), 2000)) return 17;
    auto state = service.FindState(subject, slot.Value());
    if (!state || state->dynamic.exposures.size() != 1 || state->dynamic.contents.size() != 1) return 7;

    MaterialStimulus heat{subject, slot.Value(), MaterialStimulusType::Heat, 60, {}};
    auto first = service.ApplyStimulus(heat, tags);
    if (!first || !first.Value().empty()) return 8;
    auto second = service.ApplyStimulus(heat, tags);
    if (!second || second.Value().size() != 1 || second.Value().front().type != ignition.response_type) return 9;

    const auto no_op_revision = service.CurrentRevision();
    if (!service.AssignComposition(subject, slot.Value(), composition)) return 18;
    if (service.CurrentRevision() != no_op_revision) return 19;

    const auto snapshot = service.CaptureSnapshot();
    if (snapshot.states.size() != 1) return 10;

    const GameplayObjectRef filtered_out_subject{GameplayDomainId::FromString("test.domain"), GameplayObjectId::FromString("test.filtered_out")};
    if (!service.AssignComposition(filtered_out_subject, slot.Value(), composition)) return 25;
    const std::array<GameplayObjectRef, 2> filtered_subjects{subject, subject};
    const auto filtered_snapshot = service.CaptureSnapshot(filtered_subjects);
    if (filtered_snapshot.states.size() != 1 || filtered_snapshot.states.front().key.subject != subject) return 26;
    const auto full_snapshot_after_filter_setup = service.CaptureSnapshot();
    if (full_snapshot_after_filter_setup.states.size() != 2) return 27;

    if (!service.RemoveSubstanceExposure(subject, slot.Value(), water_id.Value())) return 11;
    if (!service.RemoveContainedSubstance(subject, slot.Value(), water_id.Value())) return 20;
    if (!service.RestoreSnapshot(snapshot)) return 12;
    state = service.FindState(subject, slot.Value());
    if (!state || state->dynamic.temperature_micro != 120 || state->dynamic.exposures.size() != 1 || state->dynamic.contents.size() != 1) return 13;
    if (service.FindState(filtered_out_subject, slot.Value())) return 28;

    if (!service.AssignComposition(filtered_out_subject, slot.Value(), composition)) return 29;
    const std::array<GameplayObjectRef, 1> only_subject{subject};
    const auto partial_snapshot = service.CaptureSnapshot(only_subject);
    if (!service.RestoreSnapshot(partial_snapshot)) return 30;
    if (!service.FindState(subject, slot.Value()) || service.FindState(filtered_out_subject, slot.Value())) return 31;

    // Sparse-state stress.
    for (int i = 0; i < 10000; ++i)
    {
        const GameplayObjectRef object_ref{GameplayDomainId::FromString("test.domain"), GameplayObjectId::FromRaw(55, static_cast<std::uint64_t>(i + 1))};
        MaterialComposition c{{MaterialConstituent{wood_id.Value(), 1000000}}};
        if (!service.AssignComposition(object_ref, slot.Value(), std::move(c))) return 14;
    }
    if (service.GetDiagnostics().material_states != 10001) return 15;

    auto exhausted_revision_snapshot = service.CaptureSnapshot();
    exhausted_revision_snapshot.revision = Revision{std::numeric_limits<std::uint64_t>::max()};
    for (auto& restored_state : exhausted_revision_snapshot.states)
    {
        restored_state.revision = exhausted_revision_snapshot.revision;
    }
    if (!service.RestoreSnapshot(exhausted_revision_snapshot)) return 23;
    const GameplayObjectRef after_exhaustion{GameplayDomainId::FromString("test.domain"), GameplayObjectId::FromString("test.after_exhaustion")};
    MaterialComposition after_exhaustion_composition{{MaterialConstituent{wood_id.Value(), 1'000'000}}};
    if (service.AssignComposition(after_exhaustion, slot.Value(), after_exhaustion_composition)) return 24;

    MaterialDefinition invalid_material;
    invalid_material.canonical_name = "test.material.invalid";
    invalid_material.physical.density_micro = -1;
    MaterialService invalid_service;
    if (invalid_service.RegisterMaterial(invalid_material)) return 21;

    // Forged stimulus enum must fail without changing material state or revision.
    auto invalid_stimulus = heat;
    invalid_stimulus.type = static_cast<MaterialStimulusType>(999);
    const auto material_invalid_revision = service.CurrentRevision();
    const auto material_invalid_state = *service.FindState(subject, slot.Value());
    if (service.ApplyStimulus(invalid_stimulus, tags)) return 32;
    const auto material_after_invalid = service.FindState(subject, slot.Value());
    if (!material_after_invalid || service.CurrentRevision() != material_invalid_revision ||
        !(material_after_invalid->dynamic == material_invalid_state.dynamic))
        return 33;

    // Revision exhaustion is rejected before a material mutation is published.
    auto material_exhausted_snapshot = service.CaptureSnapshot();
    material_exhausted_snapshot.revision.value = std::numeric_limits<std::uint64_t>::max();
    if (!service.RestoreSnapshot(material_exhausted_snapshot)) return 34;
    const auto material_exhausted_before = *service.FindState(subject, slot.Value());
    if (service.SetContainedSubstance(subject, slot.Value(), water_id.Value(), 3000)) return 35;
    const auto material_exhausted_after = service.FindState(subject, slot.Value());
    if (!material_exhausted_after || service.CurrentRevision().value != std::numeric_limits<std::uint64_t>::max() ||
        !(material_exhausted_after->dynamic == material_exhausted_before.dynamic))
        return 36;

    return 0;
}


