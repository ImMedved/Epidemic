#include "allocation_fault_injection.h"
#include "Epidemic/GameFramework/Perception/perception.h"
#include "Epidemic/Foundation/error.h"

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::perception;

namespace
{
GameplayObjectRef Ref(const char *name)
{
    return {GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString(name)};
}
PerceiverEvaluationSample Sample(GameplayObjectRef subject, WorldPosition position = {}, WorldDirection forward = {})
{
    PerceiverEvaluationSample sample;
    sample.subject = subject;
    sample.position = position;
    sample.forward = forward;
    sample.materialized = true;
    sample.runtime_projection_available = true;
    return sample;
}
class ThrowingEvaluator final : public ISenseEvaluator
{
  public:
    epidemic::foundation::Result<SenseEvaluationResult> Evaluate(const SenseEvaluationInput &) const override
    {
        throw std::runtime_error("test");
    }
};

class AbstractNoSampleEvaluator final : public ISenseEvaluator
{
  public:
    epidemic::foundation::Result<SenseEvaluationResult> Evaluate(const SenseEvaluationInput &input) const override
    {
        SenseEvaluationResult result;
        result.score_micro = 1'000'000;
        result.identified_subject = true;
        result.identity_confidence_micro = 1'000'000;
        result.perceived_position = input.stimulus.position;
        return epidemic::foundation::Result<SenseEvaluationResult>::Success(result);
    }
};

class ZeroEvaluator final : public ISenseEvaluator
{
  public:
    epidemic::foundation::Result<SenseEvaluationResult> Evaluate(const SenseEvaluationInput &) const override
    {
        SenseEvaluationResult result;
        return epidemic::foundation::Result<SenseEvaluationResult>::Success(result);
    }
};

class FailingEvaluator final : public ISenseEvaluator
{
  public:
    epidemic::foundation::Result<SenseEvaluationResult> Evaluate(const SenseEvaluationInput &) const override
    {
        return epidemic::foundation::Result<SenseEvaluationResult>::Failure(
            epidemic::foundation::Error::Create("test.perception.evaluator_failure", "expected evaluator failure"));
    }
};

class CountingEvaluator final : public ISenseEvaluator
{
  public:
    mutable std::vector<GameplayObjectRef> subjects;
    epidemic::foundation::Result<SenseEvaluationResult> Evaluate(const SenseEvaluationInput &input) const override
    {
        subjects.push_back(input.perceiver.subject);
        SenseEvaluationResult result;
        return epidemic::foundation::Result<SenseEvaluationResult>::Success(result);
    }
};
} // namespace

int main()
{
    const auto hearing = SenseTypeId::FromString("framework.sense.hearing");
    const auto vision = SenseTypeId::FromString("framework.sense.vision");
    const auto custom = SenseTypeId::FromString("test.sense.custom");
    const auto evaluator_id = SenseEvaluatorId::FromString("test.evaluator.throw");
    const auto profile_id = PerceiverProfileId::FromString("test.npc.perception");
    const auto npc = Ref("npc");
    const auto player = Ref("player");

    SenseDefinition hear;
    hear.id = hearing;
    hear.canonical_name = "framework.sense.hearing";
    hear.evaluation_model = SenseEvaluationModel::Hearing;
    hear.base_range_mm = 10'000;

    SenseDefinition see;
    see.id = vision;
    see.canonical_name = "framework.sense.vision";
    see.evaluation_model = SenseEvaluationModel::Vision;
    see.base_range_mm = 10'000;
    see.field_of_view_cosine_micro = 0; // 180 degree full FOV.

    PerceptionService service;
    if (!service.RegisterSense(hear) || !service.RegisterSense(see))
        return 1;

    PerceiverProfileDefinition profile;
    profile.id = profile_id;
    profile.canonical_name = "test.npc.perception";
    profile.senses = {hearing, vision};
    if (!service.RegisterProfileDefinition(profile))
        return 2;

    service.Freeze();
    if (!service.RegisterPerceiver(npc, profile_id))
        return 3;

    PerceptionStimulus audible;
    audible.sense = hearing;
    audible.source = player;
    audible.position = {1'000, 0, 0};
    audible.strength_micro = 900'000;
    audible.created_at = {0};
    audible.lifetime = {100};

    auto sid = service.CreateStimulus(audible);
    if (!sid)
        return 4;
    auto observations = service.ProcessStimulus(
        sid.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {Sample(npc)}});
    if (!observations || observations.Value().size() != 1)
        return 5;
    if (observations.Value()[0].perceiver != npc || observations.Value()[0].perceived_subject != player)
        return 6;
    if (!service.GetAwareness(npc, player))
        return 7;

    // Dynamic perceivers are runtime state and can be created after definitions are frozen.
    const auto npc2 = Ref("npc2");
    if (!service.RegisterPerceiver(npc2, profile_id))
        return 8;
    if (!service.UnregisterPerceiver(npc2))
        return 9;

    // Hearing obeys range and never falls back to origin if a sample is missing.
    PerceptionStimulus far = audible;
    far.id = {};
    far.position = {20'000, 0, 0};
    auto far_id = service.CreateStimulus(far);
    if (!far_id)
        return 10;
    auto far_result = service.ProcessStimulus(
        far_id.Value(), PerceptionProcessingContext{GameplayTickId{2}, GameplayTimePoint{2}, {}, {Sample(npc)}});
    if (!far_result || !far_result.Value().empty())
        return 11;
    auto missing_sample = service.ProcessStimulus(
        far_id.Value(), PerceptionProcessingContext{GameplayTickId{3}, GameplayTimePoint{3}, {}, {}});
    if (!missing_sample || !missing_sample.Value().empty())
        return 12;

    // Vision uses the declared Vision model and FOV. A target behind the perceiver is hidden.
    PerceptionStimulus visual = audible;
    visual.id = {};
    visual.sense = vision;
    visual.position = {1'000, 0, 0};
    visual.strength_micro = 1'000'000;
    auto visual_id = service.CreateStimulus(visual);
    if (!visual_id)
        return 13;
    auto front = service.ProcessStimulus(
        visual_id.Value(),
        PerceptionProcessingContext{GameplayTickId{4}, GameplayTimePoint{4}, {}, {Sample(npc, {}, {1'000'000, 0, 0})}});
    if (!front || front.Value().size() != 1)
        return 14;

    PerceptionStimulus behind = visual;
    behind.id = {};
    behind.position = {-1'000, 0, 0};
    auto behind_id = service.CreateStimulus(behind);
    if (!behind_id)
        return 15;
    auto rear = service.ProcessStimulus(
        behind_id.Value(),
        PerceptionProcessingContext{GameplayTickId{5}, GameplayTimePoint{5}, {}, {Sample(npc, {}, {1'000'000, 0, 0})}});
    if (!rear || !rear.Value().empty())
        return 16;

    PerceptionService blind;
    SenseDefinition blind_vision = see;
    blind_vision.acuity_micro = 0;
    if (!blind.RegisterSense(blind_vision))
        return 61;
    PerceiverProfileDefinition blind_profile;
    blind_profile.id = PerceiverProfileId::FromString("test.blind");
    blind_profile.canonical_name = "test.blind";
    blind_profile.senses = {vision};
    if (!blind.RegisterProfileDefinition(blind_profile))
        return 62;
    blind.Freeze();
    if (!blind.RegisterPerceiver(npc, blind_profile.id))
        return 63;
    auto blind_sid = blind.CreateStimulus(visual);
    if (!blind_sid)
        return 64;
    auto blind_out = blind.ProcessStimulus(
        blind_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {Sample(npc, {}, {1'000'000, 0, 0})}});
    if (!blind_out || !blind_out.Value().empty())
        return 65;

    // Weak hearing creates an anonymous observation and does not create target awareness.
    PerceptionService anonymous_service;
    SenseDefinition weak_hearing = hear;
    weak_hearing.base_range_mm = 10'000;
    weak_hearing.identify_threshold_micro = 800'000;
    weak_hearing.direction_threshold_micro = 300'000;
    if (!anonymous_service.RegisterSense(weak_hearing))
        return 17;
    PerceiverProfileDefinition weak_profile;
    weak_profile.id = PerceiverProfileId::FromString("test.weak");
    weak_profile.canonical_name = "test.weak";
    weak_profile.senses = {hearing};
    if (!anonymous_service.RegisterProfileDefinition(weak_profile))
        return 18;
    anonymous_service.Freeze();
    if (!anonymous_service.RegisterPerceiver(npc, weak_profile.id))
        return 19;
    PerceptionStimulus weak = audible;
    weak.id = {};
    weak.strength_micro = 500'000;
    weak.position = {2'000, 0, 0};
    auto weak_id = anonymous_service.CreateStimulus(weak);
    if (!weak_id)
        return 20;
    auto weak_out = anonymous_service.ProcessStimulus(
        weak_id.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {Sample(npc)}});
    if (!weak_out || weak_out.Value().size() != 1 || weak_out.Value()[0].perceived_subject.IsValid() ||
        weak_out.Value()[0].position_uncertainty_mm <= 0)
        return 21;
    if (anonymous_service.GetAwareness(npc, player))
        return 22;

    // Reaction delay parks the observation until AdvanceTime.
    PerceptionService delayed;
    SenseDefinition delayed_hearing = hear;
    delayed_hearing.reaction_delay = GameplayDuration{5};
    if (!delayed.RegisterSense(delayed_hearing))
        return 23;
    PerceiverProfileDefinition delayed_profile;
    delayed_profile.id = PerceiverProfileId::FromString("test.delayed");
    delayed_profile.canonical_name = "test.delayed";
    delayed_profile.senses = {hearing};
    if (!delayed.RegisterProfileDefinition(delayed_profile))
        return 24;
    delayed.Freeze();
    if (!delayed.RegisterPerceiver(npc, delayed_profile.id))
        return 25;
    auto delayed_sid = delayed.CreateStimulus(audible);
    if (!delayed_sid)
        return 26;
    auto delayed_now = delayed.ProcessStimulus(
        delayed_sid.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{10}, {}, {Sample(npc)}});
    if (!delayed_now || !delayed_now.Value().empty() || delayed.GetDiagnostics().pending_observations != 1)
        return 27;
    auto before_ready = delayed.AdvanceTime({14});
    if (!before_ready || !before_ready.Value().empty())
        return 28;
    auto at_ready = delayed.AdvanceTime({15});
    if (!at_ready || at_ready.Value().size() != 1 || delayed.GetDiagnostics().pending_observations != 0)
        return 29;

    // Materialization policy is enforced by current processing samples.
    PerceptionService projected;
    if (!projected.RegisterSense(hear))
        return 30;
    PerceiverProfileDefinition projected_profile;
    projected_profile.id = PerceiverProfileId::FromString("test.projected");
    projected_profile.canonical_name = "test.projected";
    projected_profile.senses = {hearing};
    projected_profile.materialization_policy = PerceptionMaterializationPolicy::RequiresRuntimeProjection;
    if (!projected.RegisterProfileDefinition(projected_profile))
        return 31;
    projected.Freeze();
    if (!projected.RegisterPerceiver(npc, projected_profile.id))
        return 32;
    auto projected_sid = projected.CreateStimulus(audible);
    if (!projected_sid)
        return 33;
    auto unavailable_sample = Sample(npc);
    unavailable_sample.runtime_projection_available = false;
    auto projected_out = projected.ProcessStimulus(
        projected_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {unavailable_sample}});
    if (!projected_out || !projected_out.Value().empty())
        return 34;

    // Detection budget applies to all senses before evaluation.
    PerceptionService budgeted;
    if (!budgeted.RegisterSense(hear))
        return 35;
    PerceiverProfileDefinition budget_profile;
    budget_profile.id = PerceiverProfileId::FromString("test.budget");
    budget_profile.canonical_name = "test.budget";
    budget_profile.senses = {hearing};
    if (!budgeted.RegisterProfileDefinition(budget_profile))
        return 36;
    budgeted.Freeze();
    if (!budgeted.RegisterPerceiver(npc, budget_profile.id) || !budgeted.RegisterPerceiver(npc2, budget_profile.id))
        return 37;
    budgeted.SetBudget({10, 10, 1});
    auto budget_sid = budgeted.CreateStimulus(audible);
    if (!budget_sid)
        return 38;
    auto budget_out = budgeted.ProcessStimulus(
        budget_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {Sample(npc), Sample(npc2)}});
    if (!budget_out || budgeted.GetDiagnostics().detection_tests != 1 ||
        budgeted.GetDiagnostics().budget_exhaustions == 0)
        return 39;

    // M04: unavailable built-in candidates are filtered before the per-stimulus candidate cap.
    PerceptionService prefiltered;
    if (!prefiltered.RegisterSense(hear))
        return 84;
    PerceiverProfileDefinition prefilter_profile;
    prefilter_profile.id = PerceiverProfileId::FromString("test.prefilter");
    prefilter_profile.canonical_name = "test.prefilter";
    prefilter_profile.senses = {hearing};
    if (!prefiltered.RegisterProfileDefinition(prefilter_profile))
        return 85;
    prefiltered.Freeze();
    auto lower = Ref("prefilter.a");
    auto higher = Ref("prefilter.b");
    if (higher < lower)
        std::swap(lower, higher);
    if (!prefiltered.RegisterPerceiver(lower, prefilter_profile.id) ||
        !prefiltered.RegisterPerceiver(higher, prefilter_profile.id))
        return 86;
    prefiltered.SetBudget({10, 1, 10});
    auto prefilter_sid = prefiltered.CreateStimulus(audible);
    if (!prefilter_sid)
        return 87;
    auto prefilter_out = prefiltered.ProcessStimulus(
        prefilter_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {Sample(higher)}});
    if (!prefilter_out || prefilter_out.Value().size() != 1 || prefilter_out.Value().front().perceiver != higher ||
        prefiltered.GetDiagnostics().detection_tests != 1)
        return 88;

    // RequiresMaterialized candidates with a non-materialized sample also do not consume the candidate slot.
    PerceptionService materialized_prefilter;
    if (!materialized_prefilter.RegisterSense(hear))
        return 89;
    PerceiverProfileDefinition materialized_prefilter_profile;
    materialized_prefilter_profile.id = PerceiverProfileId::FromString("test.prefilter.materialized");
    materialized_prefilter_profile.canonical_name = "test.prefilter.materialized";
    materialized_prefilter_profile.senses = {hearing};
    materialized_prefilter_profile.materialization_policy = PerceptionMaterializationPolicy::RequiresMaterialized;
    if (!materialized_prefilter.RegisterProfileDefinition(materialized_prefilter_profile))
        return 90;
    materialized_prefilter.Freeze();
    auto materialized_lower = Ref("prefilter.materialized.a");
    auto materialized_higher = Ref("prefilter.materialized.b");
    if (materialized_higher < materialized_lower)
        std::swap(materialized_lower, materialized_higher);
    if (!materialized_prefilter.RegisterPerceiver(materialized_lower, materialized_prefilter_profile.id) ||
        !materialized_prefilter.RegisterPerceiver(materialized_higher, materialized_prefilter_profile.id))
        return 91;
    materialized_prefilter.SetBudget({10, 1, 10});
    auto unavailable_materialized_sample = Sample(materialized_lower);
    unavailable_materialized_sample.materialized = false;
    auto materialized_prefilter_sid = materialized_prefilter.CreateStimulus(audible);
    if (!materialized_prefilter_sid)
        return 92;
    auto materialized_prefilter_out = materialized_prefilter.ProcessStimulus(
        materialized_prefilter_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {},
                                    {unavailable_materialized_sample, Sample(materialized_higher)}});
    if (!materialized_prefilter_out || materialized_prefilter_out.Value().size() != 1 ||
        materialized_prefilter_out.Value().front().perceiver != materialized_higher ||
        materialized_prefilter.GetDiagnostics().detection_tests != 1)
        return 93;

    // Custom AbstractCapable evaluators that do not require a sample remain eligible for prefiltering.
    PerceptionService abstract_custom;
    const auto abstract_custom_sense = SenseTypeId::FromString("test.sense.abstract_custom");
    const auto abstract_custom_evaluator_id = SenseEvaluatorId::FromString("test.evaluator.abstract_custom");
    SenseDefinition abstract_custom_definition;
    abstract_custom_definition.id = abstract_custom_sense;
    abstract_custom_definition.canonical_name = "test.sense.abstract_custom";
    abstract_custom_definition.evaluation_model = SenseEvaluationModel::Custom;
    abstract_custom_definition.evaluator = abstract_custom_evaluator_id;
    abstract_custom_definition.requires_spatial_sample = false;
    if (!abstract_custom.RegisterEvaluator(abstract_custom_evaluator_id, std::make_shared<AbstractNoSampleEvaluator>()) ||
        !abstract_custom.RegisterSense(abstract_custom_definition))
        return 94;
    PerceiverProfileDefinition abstract_custom_profile;
    abstract_custom_profile.id = PerceiverProfileId::FromString("test.abstract_custom.profile");
    abstract_custom_profile.canonical_name = "test.abstract_custom.profile";
    abstract_custom_profile.senses = {abstract_custom_sense};
    abstract_custom_profile.materialization_policy = PerceptionMaterializationPolicy::AbstractCapable;
    if (!abstract_custom.RegisterProfileDefinition(abstract_custom_profile))
        return 95;
    abstract_custom.Freeze();
    if (!abstract_custom.RegisterPerceiver(npc, abstract_custom_profile.id))
        return 96;
    abstract_custom.SetBudget({10, 1, 10});
    PerceptionStimulus abstract_custom_stimulus = audible;
    abstract_custom_stimulus.id = {};
    abstract_custom_stimulus.sense = abstract_custom_sense;
    auto abstract_custom_sid = abstract_custom.CreateStimulus(abstract_custom_stimulus);
    if (!abstract_custom_sid)
        return 97;
    auto abstract_custom_out = abstract_custom.ProcessStimulus(
        abstract_custom_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {}});
    if (!abstract_custom_out || abstract_custom_out.Value().size() != 1 ||
        abstract_custom_out.Value().front().perceiver != npc || abstract_custom.GetDiagnostics().custom_tests != 1)
        return 98;

    // Custom evaluator exceptions are contained at the Framework boundary.
    PerceptionService custom_service;
    SenseDefinition custom_def;
    custom_def.id = custom;
    custom_def.canonical_name = "test.sense.custom";
    custom_def.evaluation_model = SenseEvaluationModel::Custom;
    custom_def.evaluator = evaluator_id;
    custom_def.requires_spatial_sample = false;
    if (!custom_service.RegisterEvaluator(evaluator_id, std::make_shared<ThrowingEvaluator>()) ||
        !custom_service.RegisterSense(custom_def))
        return 40;
    PerceiverProfileDefinition custom_profile;
    custom_profile.id = PerceiverProfileId::FromString("test.custom.profile");
    custom_profile.canonical_name = "test.custom.profile";
    custom_profile.senses = {custom};
    if (!custom_service.RegisterProfileDefinition(custom_profile))
        return 41;
    custom_service.Freeze();
    if (!custom_service.RegisterPerceiver(npc, custom_profile.id))
        return 42;
    PerceptionStimulus custom_stim = audible;
    custom_stim.id = {};
    custom_stim.sense = custom;
    auto custom_sid = custom_service.CreateStimulus(custom_stim);
    if (!custom_sid)
        return 43;
    auto custom_out = custom_service.ProcessStimulus(
        custom_sid.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {}});
    if (custom_out || custom_service.GetDiagnostics().evaluator_failures != 1)
        return 44;

    // A normal zero-score custom evaluation is successful non-detection, while evaluator failures propagate.
    const auto zero_sense = SenseTypeId::FromString("test.sense.zero");
    const auto zero_evaluator_id = SenseEvaluatorId::FromString("test.evaluator.zero");
    PerceptionService zero_service;
    SenseDefinition zero_definition;
    zero_definition.id = zero_sense;
    zero_definition.canonical_name = "test.sense.zero";
    zero_definition.evaluation_model = SenseEvaluationModel::Custom;
    zero_definition.evaluator = zero_evaluator_id;
    zero_definition.requires_spatial_sample = false;
    if (!zero_service.RegisterEvaluator(zero_evaluator_id, std::make_shared<ZeroEvaluator>()) ||
        !zero_service.RegisterSense(zero_definition))
        return 100;
    PerceiverProfileDefinition zero_profile;
    zero_profile.id = PerceiverProfileId::FromString("test.zero.profile");
    zero_profile.canonical_name = "test.zero.profile";
    zero_profile.senses = {zero_sense};
    if (!zero_service.RegisterProfileDefinition(zero_profile))
        return 101;
    zero_service.Freeze();
    if (!zero_service.RegisterPerceiver(npc, zero_profile.id))
        return 102;
    PerceptionStimulus zero_stimulus = audible;
    zero_stimulus.id = {};
    zero_stimulus.sense = zero_sense;
    auto zero_sid = zero_service.CreateStimulus(zero_stimulus);
    if (!zero_sid)
        return 103;
    auto zero_out = zero_service.ProcessStimulus(
        zero_sid.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {}});
    if (!zero_out || !zero_out.Value().empty() || zero_service.GetDiagnostics().evaluator_failures != 0)
        return 104;

    const auto failing_sense = SenseTypeId::FromString("test.sense.failure");
    const auto failing_evaluator_id = SenseEvaluatorId::FromString("test.evaluator.failure");
    PerceptionService failing_service;
    SenseDefinition failing_definition = zero_definition;
    failing_definition.id = failing_sense;
    failing_definition.canonical_name = "test.sense.failure";
    failing_definition.evaluator = failing_evaluator_id;
    if (!failing_service.RegisterEvaluator(failing_evaluator_id, std::make_shared<FailingEvaluator>()) ||
        !failing_service.RegisterSense(failing_definition))
        return 105;
    PerceiverProfileDefinition failing_profile = zero_profile;
    failing_profile.id = PerceiverProfileId::FromString("test.failure.profile");
    failing_profile.canonical_name = "test.failure.profile";
    failing_profile.senses = {failing_sense};
    if (!failing_service.RegisterProfileDefinition(failing_profile))
        return 106;
    failing_service.Freeze();
    if (!failing_service.RegisterPerceiver(npc, failing_profile.id))
        return 107;
    PerceptionStimulus failing_stimulus = audible;
    failing_stimulus.id = {};
    failing_stimulus.sense = failing_sense;
    auto failing_sid = failing_service.CreateStimulus(failing_stimulus);
    if (!failing_sid)
        return 108;
    auto failing_out = failing_service.ProcessStimulus(
        failing_sid.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {}});
    if (failing_out || failing_service.GetDiagnostics().evaluator_failures != 1)
        return 109;

    // Missing custom evaluator is rejected structurally rather than becoming a successful non-detection path.
    PerceptionService missing_evaluator_service;
    SenseDefinition missing_definition = zero_definition;
    missing_definition.id = SenseTypeId::FromString("test.sense.missing_evaluator");
    missing_definition.canonical_name = "test.sense.missing_evaluator";
    missing_definition.evaluator = SenseEvaluatorId::FromString("test.evaluator.missing");
    if (missing_evaluator_service.RegisterSense(missing_definition))
        return 110;

    // Processing input rejects duplicate samples before any temporal or awareness mutation.
    PerceptionService duplicate_samples;
    if (!duplicate_samples.RegisterSense(hear))
        return 111;
    PerceiverProfileDefinition duplicate_profile;
    duplicate_profile.id = PerceiverProfileId::FromString("test.duplicate.profile");
    duplicate_profile.canonical_name = "test.duplicate.profile";
    duplicate_profile.senses = {hearing};
    if (!duplicate_samples.RegisterProfileDefinition(duplicate_profile))
        return 112;
    duplicate_samples.Freeze();
    if (!duplicate_samples.RegisterPerceiver(npc, duplicate_profile.id))
        return 113;
    auto duplicate_sid = duplicate_samples.CreateStimulus(audible);
    if (!duplicate_sid)
        return 114;
    const auto duplicate_revision = duplicate_samples.CurrentRevision();
    auto duplicate_out = duplicate_samples.ProcessStimulus(
        duplicate_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{50}, {},
                                    {Sample(npc, {0, 0, 0}), Sample(npc, {9'000, 0, 0})}});
    if (duplicate_out || duplicate_samples.CurrentRevision() != duplicate_revision ||
        duplicate_samples.GetAwareness(npc, player) || !duplicate_samples.FindObservationsByPerceiver(npc).empty())
        return 115;

    // Candidate discovery indexes supplied samples once and preserves canonical evaluation order.
    const auto counted_sense = SenseTypeId::FromString("test.sense.counted");
    const auto counted_evaluator_id = SenseEvaluatorId::FromString("test.evaluator.counted");
    auto counted_evaluator = std::make_shared<CountingEvaluator>();
    PerceptionService indexed_candidates;
    SenseDefinition counted_definition;
    counted_definition.id = counted_sense;
    counted_definition.canonical_name = "test.sense.counted";
    counted_definition.evaluation_model = SenseEvaluationModel::Custom;
    counted_definition.evaluator = counted_evaluator_id;
    counted_definition.requires_spatial_sample = true;
    if (!indexed_candidates.RegisterEvaluator(counted_evaluator_id, counted_evaluator) ||
        !indexed_candidates.RegisterSense(counted_definition))
        return 116;
    PerceiverProfileDefinition counted_profile;
    counted_profile.id = PerceiverProfileId::FromString("test.counted.profile");
    counted_profile.canonical_name = "test.counted.profile";
    counted_profile.senses = {counted_sense};
    if (!indexed_candidates.RegisterProfileDefinition(counted_profile))
        return 117;
    indexed_candidates.Freeze();
    std::vector<GameplayObjectRef> many_perceivers;
    for (int i = 0; i < 256; ++i)
    {
        const auto name = "indexed_" + std::to_string(i);
        many_perceivers.push_back(Ref(name.c_str()));
        if (!indexed_candidates.RegisterPerceiver(many_perceivers.back(), counted_profile.id))
            return 118;
    }
    PerceptionStimulus counted_stimulus = audible;
    counted_stimulus.id = {};
    counted_stimulus.sense = counted_sense;
    auto counted_sid = indexed_candidates.CreateStimulus(counted_stimulus);
    if (!counted_sid)
        return 119;
    const auto indexed_lower =
        many_perceivers[17] < many_perceivers[201] ? many_perceivers[17] : many_perceivers[201];
    const auto indexed_higher =
        many_perceivers[17] < many_perceivers[201] ? many_perceivers[201] : many_perceivers[17];
    auto counted_out = indexed_candidates.ProcessStimulus(
        counted_sid.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {},
                                                         {Sample(indexed_higher), Sample(indexed_lower)}});
    if (!counted_out || counted_evaluator->subjects.size() != 2 ||
        counted_evaluator->subjects[0] != indexed_lower || counted_evaluator->subjects[1] != indexed_higher ||
        indexed_candidates.GetDiagnostics().detection_tests != 2)
        return 120;

    // Snapshot contains runtime perceivers and pending state, not definitions, and restore is transactional.
    PerceptionStimulus delayed_again = audible;
    delayed_again.id = {};
    auto delayed_again_sid = delayed.CreateStimulus(delayed_again);
    if (!delayed_again_sid)
        return 66;
    auto pending_before_save = delayed.ProcessStimulus(
        delayed_again_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{2}, GameplayTimePoint{20}, {}, {Sample(npc)}});
    if (!pending_before_save || !pending_before_save.Value().empty() || delayed.GetDiagnostics().pending_observations != 1)
        return 67;
    const auto saved_change_cursor = delayed.ReadChangesSince(ChangeCursor{}).latest_cursor;
    auto snapshot = delayed.CaptureSnapshot();
    PerceptionService restored;
    if (!restored.RegisterSense(delayed_hearing) || !restored.RegisterProfileDefinition(delayed_profile))
        return 45;
    restored.Freeze();
    if (!restored.RestoreSnapshot(snapshot))
        return 46;
    if (!restored.FindPerceiver(npc))
        return 47;
    if (!restored.ReadChangesSince(saved_change_cursor).snapshot_required)
        return 121;
    const auto restored_change_cursor = restored.LatestChangeCursor();
    auto restored_pending = restored.AdvanceTime({25});
    if (!restored_pending || restored_pending.Value().size() != 1 || restored.GetDiagnostics().pending_observations != 0)
        return 68;
    const auto resumed_changes = restored.ReadChangesSince(restored_change_cursor);
    if (resumed_changes.snapshot_required || resumed_changes.changes.empty() ||
        resumed_changes.changes.front().sequence <= restored_change_cursor.sequence)
        return 122;
    if (restored_change_cursor.sequence > 0 &&
        !restored.ReadChangesSince(restored_change_cursor.AtSequence(restored_change_cursor.sequence - 1)).snapshot_required)
        return 123;
    const auto restored_latest = resumed_changes.latest_cursor;
    if (restored_latest.sequence != std::numeric_limits<std::uint64_t>::max() &&
        !restored.ReadChangesSince(restored_latest.AtSequence(restored_latest.sequence + 1)).snapshot_required)
        return 128;

    auto exhausted_sequence_snapshot = snapshot;
    exhausted_sequence_snapshot.next_change_sequence = std::numeric_limits<std::uint64_t>::max();
    PerceptionService exhausted_sequence;
    if (!exhausted_sequence.RegisterSense(delayed_hearing) ||
        !exhausted_sequence.RegisterProfileDefinition(delayed_profile))
        return 124;
    exhausted_sequence.Freeze();
    if (!exhausted_sequence.RestoreSnapshot(exhausted_sequence_snapshot))
        return 125;
    const auto exhausted_subject = Ref("sequence.max");
    if (!exhausted_sequence.RegisterPerceiver(exhausted_subject, delayed_profile.id))
        return 126;
    auto max_batch = exhausted_sequence.ReadChangesSince(exhausted_sequence.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max() - 1));
    if (max_batch.snapshot_required || max_batch.changes.size() != 1 ||
        max_batch.changes.front().sequence != std::numeric_limits<std::uint64_t>::max())
        return 127;

    auto corrupt = snapshot;
    corrupt.stimulus_ids.scope ^= 0xFFu;
    const auto before_revision = restored.CurrentRevision();
    if (restored.RestoreSnapshot(corrupt))
        return 48;
    if (restored.CurrentRevision() != before_revision || !restored.FindPerceiver(npc))
        return 49;

    // Bounded journal reports when the caller is behind retained history.
    restored.SetChangeJournalCapacity(2);
    const auto another = Ref("another");
    if (!restored.RegisterPerceiver(another, delayed_profile.id) || !restored.UnregisterPerceiver(another) ||
        !restored.RegisterPerceiver(another, delayed_profile.id) || !restored.UnregisterPerceiver(another))
        return 50;
    auto batch = restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(1));
    if (!batch.snapshot_required)
        return 51;

    // Awareness decay reaches Unaware and is physically removed when no observation remains.
    PerceptionService temporal;
    if (!temporal.RegisterSense(hear))
        return 52;
    PerceiverProfileDefinition temporal_profile;
    temporal_profile.id = PerceiverProfileId::FromString("test.temporal");
    temporal_profile.canonical_name = "test.temporal";
    temporal_profile.senses = {hearing};
    if (!temporal.RegisterProfileDefinition(temporal_profile))
        return 53;
    temporal.SetTemporalPolicy({GameplayDuration{5}, GameplayDuration{10}, 250'000});
    temporal.Freeze();
    if (!temporal.RegisterPerceiver(npc, temporal_profile.id))
        return 54;
    PerceptionStimulus temporal_stim = audible;
    temporal_stim.id = {};
    temporal_stim.strength_micro = 1'000'000;
    temporal_stim.position = {0, 0, 0};
    auto temporal_sid = temporal.CreateStimulus(temporal_stim);
    if (!temporal_sid)
        return 55;
    auto temporal_out = temporal.ProcessStimulus(
        temporal_sid.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{10}, {}, {Sample(npc)}});
    if (!temporal_out || temporal_out.Value().size() != 1)
        return 56;
    if (!temporal.AdvanceTime({30}))
        return 57;
    const auto *lost = temporal.GetAwareness(npc, player);
    if (!lost || lost->level != AwarenessLevel::Lost)
        return 58;
    if (!temporal.AdvanceTime({40}))
        return 59;
    if (temporal.GetAwareness(npc, player))
        return 60;

    PerceptionService requested_ids;
    if (!requested_ids.RegisterSense(hear) || !requested_ids.RegisterProfileDefinition(temporal_profile))
        return 69;
    requested_ids.Freeze();
    if (!requested_ids.RegisterPerceiver(npc, temporal_profile.id))
        return 70;
    PerceptionStimulus requested = audible;
    requested.id = PerceptionStimulusId::FromRaw(0x2000, 100);
    auto requested_id = requested_ids.CreateStimulus(requested);
    if (!requested_id)
        return 71;
    PerceptionStimulus generated = audible;
    generated.id = {};
    auto generated_id = requested_ids.CreateStimulus(generated);
    if (!generated_id || generated_id.Value().value.Low() <= 100)
        return 72;

    // Milestone 2: RestoreSnapshot preserves live state at allocation boundaries.
    const auto allocation_before = restored.CaptureSnapshot();
    bool saw_restore_allocation_failure = false;
    for (long long fail_after = 0; fail_after < 32; ++fail_after)
    {
        auto allocation_target = allocation_before;
        bool failed = false;
        try
        {
            epidemic::tests::allocation_fault::FailAfter fault(fail_after);
            const auto restored_under_fault = restored.RestoreSnapshot(std::move(allocation_target));
            failed = !restored_under_fault;
        }
        catch (const std::bad_alloc &)
        {
            failed = true;
        }
        if (!failed)
            break;
        saw_restore_allocation_failure = true;
        if (restored.CaptureSnapshot().revision != allocation_before.revision)
            return 933;
    }
    if (!saw_restore_allocation_failure)
        return 934;
    return 0;
}
