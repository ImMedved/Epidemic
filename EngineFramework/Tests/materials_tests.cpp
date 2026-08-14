#include "Epidemic/GameFramework/Materials/materials.h"

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
    service.Freeze();
    if (service.RegisterSlot("test.slot.late")) return 3;

    const GameplayObjectRef subject{GameplayDomainId::FromString("test.domain"), GameplayObjectId::FromString("test.object")};
    MaterialComposition bad{{MaterialConstituent{wood_id.Value(), 500000}}};
    if (service.AssignComposition(subject, slot.Value(), bad)) return 4;

    MaterialComposition composition{{MaterialConstituent{wood_id.Value(), 800000}, MaterialConstituent{iron_id.Value(), 200000}}};
    if (!service.AssignComposition(subject, slot.Value(), composition)) return 5;
    if (!service.ApplySubstanceExposure(subject, slot.Value(), water_id.Value(), 1000, 500000)) return 6;
    const auto* state = service.FindState(subject, slot.Value());
    if (state == nullptr || state->dynamic.exposures.size() != 1) return 7;

    MaterialStimulus heat{subject, slot.Value(), MaterialStimulusType::Heat, 60, {}};
    auto first = service.ApplyStimulus(heat, tags);
    if (!first || !first.Value().empty()) return 8;
    auto second = service.ApplyStimulus(heat, tags);
    if (!second || second.Value().size() != 1 || second.Value().front().type != ignition.response_type) return 9;

    const auto snapshot = service.CaptureSnapshot();
    if (snapshot.states.size() != 1) return 10;
    if (!service.RemoveSubstanceExposure(subject, slot.Value(), water_id.Value())) return 11;
    if (!service.RestoreSnapshot(snapshot)) return 12;
    state = service.FindState(subject, slot.Value());
    if (state == nullptr || state->dynamic.temperature_micro != 120 || state->dynamic.exposures.size() != 1) return 13;

    // Sparse-state stress.
    for (int i = 0; i < 10000; ++i)
    {
        const GameplayObjectRef object_ref{GameplayDomainId::FromString("test.domain"), GameplayObjectId::FromRaw(55, static_cast<std::uint64_t>(i + 1))};
        MaterialComposition c{{MaterialConstituent{wood_id.Value(), 1000000}}};
        if (!service.AssignComposition(object_ref, slot.Value(), std::move(c))) return 14;
    }
    if (service.GetDiagnostics().material_states != 10001) return 15;
    return 0;
}
