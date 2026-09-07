#include "Epidemic/GameFramework/Perception/perception.h"
#include "Epidemic/Foundation/error.h"

#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::perception
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}

constexpr Fixed kMicro = 1'000'000;

[[nodiscard]] Fixed ClampMicro(Fixed value) noexcept
{
    return std::clamp<Fixed>(value, 0, kMicro);
}

[[nodiscard]] GameplayContext EffectiveContext(GameplayContext supplied, const GameplayContext &fallback,
                                               GameplayTimePoint now) noexcept
{
    auto result = supplied.tick.IsValid() || supplied.operation.IsValid() || supplied.correlation.IsValid() ||
                          supplied.actor.IsValid() || supplied.instigator.IsValid() || supplied.source.IsValid()
                      ? supplied
                      : fallback;
    result.time = now;
    return result;
}
} // namespace

PerceptionService::PerceptionService() = default;

foundation::Result<void> PerceptionService::RegisterSense(SenseDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.registry_frozen", "perception registry frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty() || senses_.contains(d.id) || d.base_range_mm < 0 ||
        d.field_of_view_cosine_micro < -kMicro || d.field_of_view_cosine_micro > kMicro ||
        d.acuity_micro < 0 || d.acuity_micro > kMicro || d.identify_threshold_micro < 0 ||
        d.identify_threshold_micro > kMicro || d.direction_threshold_micro < 0 ||
        d.direction_threshold_micro > kMicro || d.reaction_delay.ticks < 0)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.invalid_sense", "invalid or duplicate sense"));
    }
    if (d.evaluation_model == SenseEvaluationModel::Custom &&
        (!d.evaluator.IsValid() || !evaluators_.contains(d.evaluator)))
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.invalid_sense", "custom sense requires a registered evaluator"));
    senses_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}

foundation::Result<void> PerceptionService::RegisterProfileDefinition(PerceiverProfileDefinition p)
{
    if (frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.registry_frozen", "perception registry frozen"));
    if (!p.id.IsValid() || p.canonical_name.empty() || profile_definitions_.contains(p.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.invalid_profile", "invalid or duplicate perceiver profile definition"));

    std::sort(p.senses.begin(), p.senses.end());
    if (std::adjacent_find(p.senses.begin(), p.senses.end()) != p.senses.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.invalid_profile", "duplicate sense in profile"));

    std::size_t vision_count = 0;
    std::size_t hearing_count = 0;
    for (const auto sense : p.senses)
    {
        const auto it = senses_.find(sense);
        if (it == senses_.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.unknown_sense", "profile references unknown sense"));
        vision_count += it->second.evaluation_model == SenseEvaluationModel::Vision ? 1u : 0u;
        hearing_count += it->second.evaluation_model == SenseEvaluationModel::Hearing ? 1u : 0u;
    }
    if (vision_count > 1 || hearing_count > 1)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.ambiguous_profile", "profile has multiple primary built-in senses"));

    profile_definitions_.emplace(p.id, std::move(p));
    return foundation::Result<void>::Success();
}

foundation::Result<void> PerceptionService::RegisterEvaluator(SenseEvaluatorId id,
                                                               std::shared_ptr<const ISenseEvaluator> evaluator)
{
    if (frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.registry_frozen", "perception registry frozen"));
    if (!id.IsValid() || !evaluator || evaluators_.contains(id))
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.invalid_evaluator", "invalid or duplicate sense evaluator"));
    evaluators_.emplace(id, std::move(evaluator));
    return foundation::Result<void>::Success();
}

const SenseDefinition *PerceptionService::FindSense(SenseTypeId id) const noexcept
{
    const auto it = senses_.find(id);
    return it == senses_.end() ? nullptr : &it->second;
}

const PerceiverProfileDefinition *PerceptionService::FindProfileDefinition(PerceiverProfileId id) const noexcept
{
    const auto it = profile_definitions_.find(id);
    return it == profile_definitions_.end() ? nullptr : &it->second;
}

const PerceiverRecord *PerceptionService::FindPerceiver(GameplayObjectRef subject) const noexcept
{
    const auto it = perceivers_.find(subject);
    return it == perceivers_.end() ? nullptr : &it->second;
}

foundation::Result<void> PerceptionService::RegisterPerceiver(GameplayObjectRef subject, PerceiverProfileId profile,
                                                              GameplayContext context)
{
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.registry_not_frozen", "perception definitions must be frozen"));
    if (!subject.IsValid() || !profile.IsValid() || !profile_definitions_.contains(profile) ||
        perceivers_.contains(subject))
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.invalid_perceiver", "invalid, duplicate or unknown perceiver"));
    }
    Bump();
    perceivers_.emplace(subject, PerceiverRecord{subject, profile, revision_});
    Record({0, PerceptionChangeKind::PerceiverRegistered, subject, {}, {}, {}, AwarenessLevel::Unaware, context,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> PerceptionService::UnregisterPerceiver(GameplayObjectRef subject, GameplayContext context)
{
    const auto it = perceivers_.find(subject);
    if (it == perceivers_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.perceiver_missing", "perceiver missing"));

    std::vector<PerceptionObservationId> observation_ids;
    if (const auto index = observations_by_perceiver_.find(subject); index != observations_by_perceiver_.end())
        observation_ids = index->second;
    for (const auto id : observation_ids)
    {
        const auto oi = observations_.find(id);
        if (oi != observations_.end())
        {
            UnindexObservation(oi->second);
            observations_.erase(oi);
        }
    }

    std::vector<PerceptionObservationId> pending_ids;
    for (const auto &[id, pending] : pending_observations_)
    {
        if (pending.observation.perceiver == subject)
            pending_ids.push_back(id);
    }
    std::sort(pending_ids.begin(), pending_ids.end());
    for (const auto id : pending_ids)
    {
        const auto pi = pending_observations_.find(id);
        if (pi != pending_observations_.end())
        {
            pending_by_ready_.erase(PendingKey{pi->second.ready_at, id});
            pending_observations_.erase(pi);
        }
    }

    for (auto ai = awareness_.begin(); ai != awareness_.end();)
    {
        if (ai->second.perceiver == subject)
            ai = awareness_.erase(ai);
        else
            ++ai;
    }

    perceivers_.erase(it);
    Bump();
    Record({0, PerceptionChangeKind::PerceiverUnregistered, subject, {}, {}, {}, AwarenessLevel::Unaware, context,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<PerceptionStimulusId> PerceptionService::CreateStimulus(PerceptionStimulus s)
{
    if (!frozen_)
        return foundation::Result<PerceptionStimulusId>::Failure(
            Error("gameplay.perception.registry_not_frozen", "perception definitions must be frozen"));
    if (!s.sense.IsValid() || !senses_.contains(s.sense) || !s.source.IsValid() || s.strength_micro < 0 ||
        s.strength_micro > kMicro || s.lifetime.ticks < 0)
    {
        return foundation::Result<PerceptionStimulusId>::Failure(
            Error("gameplay.perception.invalid_stimulus", "invalid stimulus"));
    }

    if (!s.id.IsValid())
    {
        const auto generated = stimulus_ids_.Next();
        if (!generated.IsValid())
            return foundation::Result<PerceptionStimulusId>::Failure(
                Error("gameplay.perception.id_exhausted", "stimulus id generator exhausted"));
        s.id = PerceptionStimulusId{generated};
    }
    if (stimuli_.contains(s.id))
        return foundation::Result<PerceptionStimulusId>::Failure(
            Error("gameplay.perception.duplicate_stimulus", "duplicate stimulus"));

    const auto generator_snapshot = stimulus_ids_.GetSnapshot();
    if (s.id.value.High() == generator_snapshot.scope && generator_snapshot.next != 0 &&
        s.id.value.Low() >= generator_snapshot.next)
    {
        auto advanced = generator_snapshot;
        advanced.next = s.id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : s.id.value.Low() + 1;
        stimulus_ids_.Restore(advanced);
    }

    Bump();
    s.revision = revision_;
    const auto id = s.id;
    stimuli_.emplace(id, s);
    IndexStimulus(s);
    Record({0, PerceptionChangeKind::StimulusCreated, s.source, {}, id, {}, AwarenessLevel::Unaware, s.context,
            revision_});
    return foundation::Result<PerceptionStimulusId>::Success(id);
}

foundation::Result<void> PerceptionService::ExpireStimuli(GameplayTimePoint now, GameplayContext context)
{
    auto temporal = AdvanceTime(now, context);
    if (!temporal)
        return foundation::Result<void>::Failure(temporal.GetError());

    std::vector<PerceptionStimulusId> expired;
    for (const auto &[id, stimulus] : stimuli_)
    {
        if (stimulus.lifetime.ticks <= 0)
            continue;
        const auto expires_at = SaturatingAdd(stimulus.created_at, stimulus.lifetime);
        if (now >= expires_at)
            expired.push_back(id);
    }
    std::sort(expired.begin(), expired.end());

    for (const auto id : expired)
    {
        const auto it = stimuli_.find(id);
        if (it == stimuli_.end())
            continue;
        const auto stimulus = it->second;
        UnindexStimulus(stimulus);
        stimuli_.erase(it);
        Bump();
        Record({0, PerceptionChangeKind::StimulusExpired, stimulus.source, {}, id, {}, AwarenessLevel::Unaware,
                EffectiveContext(context, stimulus.context, now), revision_});
    }
    return foundation::Result<void>::Success();
}

Fixed PerceptionService::DistanceSquared(WorldPosition a, WorldPosition b) noexcept
{
    const long double dx = static_cast<long double>(a.x_mm) - static_cast<long double>(b.x_mm);
    const long double dy = static_cast<long double>(a.y_mm) - static_cast<long double>(b.y_mm);
    const long double dz = static_cast<long double>(a.z_mm) - static_cast<long double>(b.z_mm);
    const long double d = dx * dx + dy * dy + dz * dz;
    return d > static_cast<long double>(std::numeric_limits<Fixed>::max()) ? std::numeric_limits<Fixed>::max()
                                                                           : static_cast<Fixed>(d);
}

Fixed PerceptionService::DistanceMm(WorldPosition a, WorldPosition b) noexcept
{
    const long double d2 = static_cast<long double>(DistanceSquared(a, b));
    const auto d = std::sqrt(std::max<long double>(0.0L, d2));
    return d >= static_cast<long double>(std::numeric_limits<Fixed>::max()) ? std::numeric_limits<Fixed>::max()
                                                                            : static_cast<Fixed>(d);
}

Fixed PerceptionService::MultiplyMicro(Fixed a, Fixed b) noexcept
{
    const long double product = static_cast<long double>(ClampMicro(a)) * static_cast<long double>(ClampMicro(b)) /
                                static_cast<long double>(kMicro);
    return std::clamp<Fixed>(static_cast<Fixed>(product), 0, kMicro);
}

Fixed PerceptionService::DistanceAttenuatedScore(Fixed strength_micro, Fixed range_mm, WorldPosition observer,
                                                  WorldPosition stimulus) noexcept
{
    if (strength_micro <= 0 || range_mm <= 0)
        return 0;
    const auto distance = static_cast<long double>(DistanceMm(observer, stimulus));
    const auto range = static_cast<long double>(range_mm);
    if (distance >= range)
        return 0;
    const auto attenuation = 1.0L - distance / range;
    const auto score = static_cast<long double>(ClampMicro(strength_micro)) * attenuation;
    return std::clamp<Fixed>(static_cast<Fixed>(score), 0, kMicro);
}

bool PerceptionService::WithinFov(const SenseDefinition &definition, const PerceiverEvaluationSample &sample,
                                  WorldPosition target) noexcept
{
    if (definition.evaluation_model != SenseEvaluationModel::Vision ||
        definition.field_of_view_cosine_micro <= -kMicro)
        return true;

    const long double dx = static_cast<long double>(target.x_mm) - static_cast<long double>(sample.position.x_mm);
    const long double dy = static_cast<long double>(target.y_mm) - static_cast<long double>(sample.position.y_mm);
    const long double dz = static_cast<long double>(target.z_mm) - static_cast<long double>(sample.position.z_mm);
    const long double target_length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const long double fx = static_cast<long double>(sample.forward.x_micro);
    const long double fy = static_cast<long double>(sample.forward.y_micro);
    const long double fz = static_cast<long double>(sample.forward.z_micro);
    const long double forward_length = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (target_length <= 0.0L || forward_length <= 0.0L)
        return false;

    const long double cosine = (dx * fx + dy * fy + dz * fz) / (target_length * forward_length);
    const long double threshold = static_cast<long double>(definition.field_of_view_cosine_micro) /
                                  static_cast<long double>(kMicro);
    return cosine >= threshold;
}

SenseEvaluationResult PerceptionService::EvaluateBuiltIn(const SenseDefinition &definition,
                                                          const PerceiverEvaluationSample &sample,
                                                          const PerceptionStimulus &stimulus) const noexcept
{
    SenseEvaluationResult result;
    result.perceived_position = sample.position;

    if (definition.requires_spatial_sample && definition.base_range_mm <= 0)
        return result;

    if (definition.evaluation_model == SenseEvaluationModel::Vision && !WithinFov(definition, sample, stimulus.position))
    {
        result.visibility = PerceptionVisibilityState::Hidden;
        return result;
    }

    Fixed score = definition.requires_spatial_sample
                      ? DistanceAttenuatedScore(stimulus.strength_micro, definition.base_range_mm, sample.position,
                                                stimulus.position)
                      : ClampMicro(stimulus.strength_micro);
    score = MultiplyMicro(score, definition.acuity_micro);
    score = MultiplyMicro(score, sample.sense_multiplier_micro);
    score = MultiplyMicro(score, sample.attenuation_micro);
    result.score_micro = score;

    const auto distance = DistanceMm(sample.position, stimulus.position);
    if (definition.evaluation_model == SenseEvaluationModel::Vision)
    {
        if (sample.attenuation_micro <= 0)
            result.visibility = PerceptionVisibilityState::Occluded;
        else if (score <= 0)
            result.visibility = PerceptionVisibilityState::Hidden;
        else
            result.visibility = score < 400'000 ? PerceptionVisibilityState::PartiallyVisible
                                                : PerceptionVisibilityState::Visible;

        result.identified_subject = score >= definition.identify_threshold_micro;
        result.identity_confidence_micro = result.identified_subject ? score : 0;
        result.perceived_position = stimulus.position;
        if (result.identified_subject)
            result.position_uncertainty_mm = 0;
        else
        {
            const long double uncertainty = static_cast<long double>(distance) *
                                            static_cast<long double>(kMicro - score) /
                                            static_cast<long double>(kMicro);
            result.position_uncertainty_mm = uncertainty >= static_cast<long double>(std::numeric_limits<Fixed>::max())
                                                 ? std::numeric_limits<Fixed>::max()
                                                 : std::max<Fixed>(1, static_cast<Fixed>(uncertainty));
        }
        return result;
    }

    if (definition.evaluation_model == SenseEvaluationModel::Hearing)
    {
        if (score <= 0)
        {
            result.audibility = PerceptionAudibilityState::NotHeard;
            return result;
        }
        if (score >= definition.identify_threshold_micro)
        {
            result.audibility = PerceptionAudibilityState::HeardExactly;
            result.identified_subject = true;
            result.identity_confidence_micro = score;
            result.perceived_position = stimulus.position;
            result.position_uncertainty_mm = 0;
        }
        else if (score >= definition.direction_threshold_micro)
        {
            result.audibility = PerceptionAudibilityState::HeardDirectionOnly;
            result.perceived_position = WorldPosition{sample.position.x_mm + (stimulus.position.x_mm - sample.position.x_mm) / 2,
                                                      sample.position.y_mm + (stimulus.position.y_mm - sample.position.y_mm) / 2,
                                                      sample.position.z_mm + (stimulus.position.z_mm - sample.position.z_mm) / 2};
            result.position_uncertainty_mm = std::max<Fixed>(1, distance / 2);
        }
        else
        {
            result.audibility = PerceptionAudibilityState::HeardVagueNoise;
            result.perceived_position = sample.position;
            result.position_uncertainty_mm = std::max<Fixed>(1, definition.base_range_mm);
        }
        return result;
    }

    if (definition.evaluation_model == SenseEvaluationModel::Radial)
    {
        result.identified_subject = score >= definition.identify_threshold_micro;
        result.identity_confidence_micro = result.identified_subject ? score : 0;
        result.perceived_position = result.identified_subject ? stimulus.position : sample.position;
        result.position_uncertainty_mm =
            result.identified_subject ? 0 : std::max<Fixed>(1, definition.base_range_mm);
    }
    return result;
}

[[nodiscard]] static bool CanAttemptSenseEvaluation(const SenseDefinition &definition,
                                             const PerceiverProfileDefinition &profile,
                                             const PerceiverEvaluationSample *sample) noexcept
{
    if (definition.requires_spatial_sample && !sample)
        return false;

    if (profile.materialization_policy == PerceptionMaterializationPolicy::RequiresMaterialized &&
        (!sample || !sample->materialized))
        return false;

    if (profile.materialization_policy == PerceptionMaterializationPolicy::RequiresRuntimeProjection &&
        (!sample || !sample->runtime_projection_available))
        return false;

    if (definition.evaluation_model != SenseEvaluationModel::Custom && !sample)
        return false;

    return true;
}

foundation::Result<SenseEvaluationResult> PerceptionService::EvaluateSense(
    const SenseDefinition &definition, const PerceiverProfileDefinition &profile, const PerceiverRecord &perceiver,
    const PerceptionStimulus &stimulus, const PerceiverEvaluationSample *sample,
    const PerceptionProcessingContext &context) const
{
    if (definition.requires_spatial_sample && !sample)
        return foundation::Result<SenseEvaluationResult>::Failure(
            Error("gameplay.perception.sample_unavailable", "spatial sample unavailable"));

    if (profile.materialization_policy != PerceptionMaterializationPolicy::AbstractCapable)
    {
        if (!sample)
            return foundation::Result<SenseEvaluationResult>::Failure(
                Error("gameplay.perception.sample_unavailable", "materialization sample unavailable"));
        if (profile.materialization_policy == PerceptionMaterializationPolicy::RequiresMaterialized &&
            !sample->materialized)
        {
            return foundation::Result<SenseEvaluationResult>::Failure(
                Error("gameplay.perception.materialization_unavailable", "perceiver is not materialized"));
        }
        if (profile.materialization_policy == PerceptionMaterializationPolicy::RequiresRuntimeProjection &&
            !sample->runtime_projection_available)
        {
            return foundation::Result<SenseEvaluationResult>::Failure(
                Error("gameplay.perception.runtime_projection_unavailable", "runtime projection unavailable"));
        }
    }

    if (definition.evaluation_model != SenseEvaluationModel::Custom)
    {
        if (!sample)
            return foundation::Result<SenseEvaluationResult>::Failure(
                Error("gameplay.perception.sample_unavailable", "built-in sense requires sample"));
        return foundation::Result<SenseEvaluationResult>::Success(EvaluateBuiltIn(definition, *sample, stimulus));
    }

    const auto evaluator = evaluators_.find(definition.evaluator);
    if (evaluator == evaluators_.end() || !evaluator->second)
    {
        ++diagnostics_.evaluator_failures;
        return foundation::Result<SenseEvaluationResult>::Failure(
            Error("gameplay.perception.evaluator_missing", "custom sense evaluator missing"));
    }
    try
    {
        auto result = evaluator->second->Evaluate(
            SenseEvaluationInput{definition, profile, perceiver, stimulus, sample, context});
        if (!result)
        {
            ++diagnostics_.evaluator_failures;
            return result;
        }
        const auto &value = result.Value();
        if (value.score_micro < 0 || value.score_micro > kMicro || value.identity_confidence_micro < 0 ||
            value.identity_confidence_micro > kMicro || value.position_uncertainty_mm < 0)
        {
            ++diagnostics_.evaluator_failures;
            return foundation::Result<SenseEvaluationResult>::Failure(
                Error("gameplay.perception.invalid_evaluator_result", "custom evaluator returned invalid result"));
        }
        return result;
    }
    catch (const std::exception &)
    {
        ++diagnostics_.evaluator_failures;
        return foundation::Result<SenseEvaluationResult>::Failure(
            Error("gameplay.perception.evaluator_exception", "sense evaluator threw exception"));
    }
    catch (...)
    {
        ++diagnostics_.evaluator_failures;
        return foundation::Result<SenseEvaluationResult>::Failure(
            Error("gameplay.perception.evaluator_exception", "sense evaluator threw unknown exception"));
    }
}

VisibilityResult PerceptionService::EvaluateVisibility(GameplayObjectRef perceiver, GameplayObjectRef target,
                                                       const PerceiverEvaluationSample &sample,
                                                       WorldPosition target_position,
                                                       Fixed stimulus_strength_micro) const
{
    VisibilityResult result;
    result.revision = revision_;
    const auto *record = FindPerceiver(perceiver);
    if (!record || !target.IsValid())
    {
        result.state = PerceptionVisibilityState::Unavailable;
        return result;
    }
    const auto *profile = FindProfileDefinition(record->profile);
    if (!profile ||
        (profile->materialization_policy == PerceptionMaterializationPolicy::RequiresMaterialized && !sample.materialized) ||
        (profile->materialization_policy == PerceptionMaterializationPolicy::RequiresRuntimeProjection &&
         !sample.runtime_projection_available))
    {
        result.state = PerceptionVisibilityState::Unavailable;
        return result;
    }
    const SenseDefinition *vision = nullptr;
    for (const auto sense : profile->senses)
    {
        const auto *definition = FindSense(sense);
        if (definition && definition->evaluation_model == SenseEvaluationModel::Vision)
        {
            vision = definition;
            break;
        }
    }
    if (!vision)
    {
        result.state = PerceptionVisibilityState::Unavailable;
        return result;
    }
    PerceptionStimulus synthetic;
    synthetic.sense = vision->id;
    synthetic.source = target;
    synthetic.position = target_position;
    synthetic.strength_micro = ClampMicro(stimulus_strength_micro);
    const auto evaluation = EvaluateBuiltIn(*vision, sample, synthetic);
    result.state = evaluation.visibility;
    result.score_micro = evaluation.score_micro;
    return result;
}

AudibilityResult PerceptionService::EvaluateAudibility(GameplayObjectRef perceiver, PerceptionStimulusId stimulus_id,
                                                        const PerceiverEvaluationSample &sample) const
{
    AudibilityResult result;
    result.revision = revision_;
    const auto *record = FindPerceiver(perceiver);
    const auto *stimulus = FindStimulus(stimulus_id);
    if (!record || !stimulus)
    {
        result.state = PerceptionAudibilityState::Unavailable;
        return result;
    }
    const auto *profile = FindProfileDefinition(record->profile);
    const auto *definition = FindSense(stimulus->sense);
    if (!profile || !definition || definition->evaluation_model != SenseEvaluationModel::Hearing ||
        !std::binary_search(profile->senses.begin(), profile->senses.end(), stimulus->sense) ||
        (profile->materialization_policy == PerceptionMaterializationPolicy::RequiresMaterialized && !sample.materialized) ||
        (profile->materialization_policy == PerceptionMaterializationPolicy::RequiresRuntimeProjection &&
         !sample.runtime_projection_available))
    {
        result.state = PerceptionAudibilityState::Unavailable;
        return result;
    }
    const auto evaluation = EvaluateBuiltIn(*definition, sample, *stimulus);
    result.state = evaluation.audibility;
    result.score_micro = evaluation.score_micro;
    return result;
}

PerceptionConfidence PerceptionService::ConfidenceFromScore(Fixed score) const noexcept
{
    if (score <= 0)
        return PerceptionConfidence::None;
    if (score < 250'000)
        return PerceptionConfidence::Low;
    if (score < 600'000)
        return PerceptionConfidence::Medium;
    if (score < 950'000)
        return PerceptionConfidence::High;
    return PerceptionConfidence::Certain;
}

foundation::Result<std::vector<PerceptionObservation>> PerceptionService::ProcessStimulus(
    PerceptionStimulusId sid, const PerceptionProcessingContext &pc)
{
    if (!frozen_)
        return foundation::Result<std::vector<PerceptionObservation>>::Failure(
            Error("gameplay.perception.registry_not_frozen", "perception definitions must be frozen"));

    // Processing samples are snapshot-like input. Validate and index them before any temporal or awareness mutation.
    std::unordered_map<GameplayObjectRef, const PerceiverEvaluationSample *, RefHash> sample_index;
    sample_index.reserve(pc.perceivers.size());
    for (const auto &sample : pc.perceivers)
    {
        if (!sample.subject.IsValid())
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(
                Error("gameplay.perception.invalid_processing_sample", "processing sample subject is invalid"));
        if (!sample_index.emplace(sample.subject, &sample).second)
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(
                Error("gameplay.perception.duplicate_processing_sample", "duplicate perceiver processing sample"));
    }

    const auto *stimulus = FindStimulus(sid);
    if (!stimulus)
        return foundation::Result<std::vector<PerceptionObservation>>::Failure(
            Error("gameplay.perception.stimulus_missing", "stimulus missing"));

    const auto *definition = FindSense(stimulus->sense);
    if (!definition)
        return foundation::Result<std::vector<PerceptionObservation>>::Failure(
            Error("gameplay.perception.invalid_sense_link", "stimulus references an unavailable sense definition"));
    if (definition->evaluation_model == SenseEvaluationModel::Custom)
    {
        const auto evaluator = evaluators_.find(definition->evaluator);
        if (evaluator == evaluators_.end() || !evaluator->second)
        {
            ++diagnostics_.evaluator_failures;
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(
                Error("gameplay.perception.evaluator_missing", "custom sense evaluator missing"));
        }
    }

    EnsureBudgetEpoch(pc.tick);
    auto temporal = AdvanceTime(pc.now, pc.gameplay);
    if (!temporal)
        return temporal;

    const auto event_context = EffectiveContext(pc.gameplay, stimulus->context, pc.now);
    if (tick_budget_.processed_stimuli >= budget_.max_stimuli_per_tick)
    {
        ++diagnostics_.budget_exhaustions;
        ++diagnostics_.dropped_stimuli;
        Record({0, PerceptionChangeKind::BudgetExceeded, stimulus->source, {}, sid, {}, AwarenessLevel::Unaware,
                event_context, revision_});
        return foundation::Result<std::vector<PerceptionObservation>>::Success({});
    }
    ++tick_budget_.processed_stimuli;
    ++diagnostics_.processed_stimuli;

    struct Candidate
    {
        PerceiverRecord record{};
        const PerceiverProfileDefinition *profile = nullptr;
        const PerceiverEvaluationSample *sample = nullptr;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(std::min<std::size_t>(perceivers_.size(), budget_.max_perceivers_per_stimulus));
    for (const auto &[subject, record] : perceivers_)
    {
        if (subject == stimulus->source)
            continue;
        const auto *profile = FindProfileDefinition(record.profile);
        if (!profile)
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(
                Error("gameplay.perception.invalid_profile_link", "perceiver references an unavailable profile"));
        if (!std::binary_search(profile->senses.begin(), profile->senses.end(), stimulus->sense))
            continue;

        const auto sample_it = sample_index.find(record.subject);
        const auto *sample = sample_it == sample_index.end() ? nullptr : sample_it->second;
        if (!CanAttemptSenseEvaluation(*definition, *profile, sample))
            continue;
        candidates.push_back(Candidate{record, profile, sample});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a.record.subject < b.record.subject;
    });
    if (candidates.size() > budget_.max_perceivers_per_stimulus)
        candidates.resize(budget_.max_perceivers_per_stimulus);

    std::vector<PerceptionObservation> activated = std::move(temporal.Value());
    bool budget_reported = false;
    for (const auto &candidate : candidates)
    {
        const auto &perceiver = candidate.record;
        if (tick_budget_.detection_tests >= budget_.max_detection_tests_per_tick)
        {
            ++diagnostics_.budget_exhaustions;
            if (!budget_reported)
            {
                Record({0, PerceptionChangeKind::BudgetExceeded, perceiver.subject, stimulus->source, sid, {},
                        AwarenessLevel::Unaware, event_context, revision_});
                budget_reported = true;
            }
            break;
        }
        ++tick_budget_.detection_tests;
        ++diagnostics_.detection_tests;

        switch (definition->evaluation_model)
        {
        case SenseEvaluationModel::Vision:
            ++diagnostics_.visibility_tests;
            break;
        case SenseEvaluationModel::Hearing:
            ++diagnostics_.audibility_tests;
            break;
        case SenseEvaluationModel::Custom:
            ++diagnostics_.custom_tests;
            break;
        case SenseEvaluationModel::Radial:
            break;
        }

        auto evaluated = EvaluateSense(*definition, *candidate.profile, perceiver, *stimulus, candidate.sample, pc);
        if (!evaluated)
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(evaluated.GetError());
        const auto evaluation = evaluated.Value();
        if (evaluation.score_micro <= 0)
            continue;

        const auto raw_id = observation_ids_.Next();
        if (!raw_id.IsValid())
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(
                Error("gameplay.perception.id_exhausted", "observation id generator exhausted"));

        const auto observation_id = PerceptionObservationId{raw_id};
        Bump();
        PerceptionObservation observation;
        observation.id = observation_id;
        observation.perceiver = perceiver.subject;
        observation.perceived_subject = evaluation.identified_subject ? stimulus->source : GameplayObjectRef{};
        observation.stimulus = sid;
        observation.sense = stimulus->sense;
        observation.confidence = ConfidenceFromScore(evaluation.score_micro);
        observation.identity_confidence_micro =
            evaluation.identified_subject ? ClampMicro(evaluation.identity_confidence_micro) : 0;
        observation.perceived_position = evaluation.perceived_position;
        observation.position_uncertainty_mm = std::max<Fixed>(0, evaluation.position_uncertainty_mm);
        observation.observed_at = pc.now;
        observation.observed_tags = stimulus->tags;
        observation.context = event_context;
        observation.revision = revision_;

        const auto ready_at = SaturatingAdd(pc.now, definition->reaction_delay);
        PendingPerceptionObservation pending{observation, ready_at, evaluation.score_micro};
        if (definition->reaction_delay.ticks > 0 && ready_at > pc.now)
        {
            pending_observations_.emplace(observation_id, pending);
            pending_by_ready_.emplace(PendingKey{ready_at, observation_id}, observation_id);
            continue;
        }

        auto activation = ActivateObservation(std::move(pending), event_context, &activated);
        if (!activation)
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(activation.GetError());
    }

    return foundation::Result<std::vector<PerceptionObservation>>::Success(std::move(activated));
}

foundation::Result<void> PerceptionService::ActivateObservation(PendingPerceptionObservation pending,
                                                                GameplayContext context,
                                                                std::vector<PerceptionObservation> *activated)
{
    auto observation = std::move(pending.observation);
    if (observations_.contains(observation.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.duplicate_observation", "duplicate observation"));

    Bump();
    observation.revision = revision_;
    observations_.emplace(observation.id, observation);
    IndexObservation(observation);

    AwarenessLevel level = AwarenessLevel::Unaware;
    if (observation.perceived_subject.IsValid())
    {
        const AwarenessKey key{observation.perceiver, observation.perceived_subject};
        auto [it, inserted] =
            awareness_.try_emplace(key, AwarenessRecord{observation.perceiver, observation.perceived_subject});
        auto &awareness = it->second;
        (void)inserted;
        awareness.suspicion_micro =
            std::min<Fixed>(kMicro, awareness.suspicion_micro + std::max<Fixed>(1, pending.detection_score_micro / 2));
        awareness.level = awareness.suspicion_micro >= 800'000
                              ? AwarenessLevel::Confirmed
                              : (awareness.suspicion_micro >= 400'000 ? AwarenessLevel::Aware
                                                                     : AwarenessLevel::Suspicious);
        awareness.last_observed_at = observation.observed_at;
        awareness.last_decay_at = observation.observed_at;
        awareness.last_known_position = observation.perceived_position;
        awareness.revision = revision_;
        level = awareness.level;
    }

    const auto event_context = context.tick.IsValid() || context.operation.IsValid() ? context : observation.context;
    Record({0, PerceptionChangeKind::Observed, observation.perceiver, observation.perceived_subject,
            observation.stimulus, observation.id, level, event_context, revision_});
    if (observation.perceived_subject.IsValid())
    {
        Record({0, PerceptionChangeKind::AwarenessChanged, observation.perceiver, observation.perceived_subject,
                observation.stimulus, observation.id, level, event_context, revision_});
    }
    if (activated)
        activated->push_back(observation);
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<PerceptionObservation>> PerceptionService::AdvanceTime(GameplayTimePoint now,
                                                                                       GameplayContext context)
{
    std::vector<PerceptionObservation> activated;
    while (!pending_by_ready_.empty())
    {
        const auto first = pending_by_ready_.begin();
        if (first->first.ready_at > now)
            break;
        const auto id = first->second;
        pending_by_ready_.erase(first);
        const auto pending_it = pending_observations_.find(id);
        if (pending_it == pending_observations_.end())
            continue;
        auto pending = std::move(pending_it->second);
        pending_observations_.erase(pending_it);
        auto result = ActivateObservation(std::move(pending), context, &activated);
        if (!result)
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(result.GetError());
    }

    ExpireObservations(now, context);
    DecayAwareness(now, context);
    CleanupUnaware();
    return foundation::Result<std::vector<PerceptionObservation>>::Success(std::move(activated));
}

void PerceptionService::ExpireObservations(GameplayTimePoint now, GameplayContext context)
{
    if (temporal_policy_.observation_retention.ticks <= 0)
        return;

    std::vector<PerceptionObservationId> expired;
    for (const auto &[id, observation] : observations_)
    {
        const auto expires_at = CheckedAdd(observation.observed_at, temporal_policy_.observation_retention);
        if (!expires_at || now >= *expires_at)
            expired.push_back(id);
    }
    std::sort(expired.begin(), expired.end());

    for (const auto id : expired)
    {
        const auto it = observations_.find(id);
        if (it == observations_.end())
            continue;
        const auto observation = it->second;
        UnindexObservation(observation);
        observations_.erase(it);
        Bump();
        Record({0, PerceptionChangeKind::Lost, observation.perceiver, observation.perceived_subject,
                observation.stimulus, observation.id, AwarenessLevel::Lost,
                EffectiveContext(context, observation.context, now), revision_});
    }
}

void PerceptionService::DecayAwareness(GameplayTimePoint now, GameplayContext context)
{
    const auto interval = temporal_policy_.awareness_decay_interval.ticks;
    const auto decay = temporal_policy_.awareness_decay_micro_per_interval;
    if (interval <= 0 || decay <= 0)
        return;

    std::vector<AwarenessKey> keys;
    keys.reserve(awareness_.size());
    for (const auto &[key, record] : awareness_)
    {
        (void)record;
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    for (const auto &key : keys)
    {
        const auto it = awareness_.find(key);
        if (it == awareness_.end())
            continue;
        auto &record = it->second;
        if (now <= record.last_decay_at)
            continue;
        const auto elapsed = CheckedDifference(now, record.last_decay_at);
        if (!elapsed || elapsed->ticks <= 0)
            continue;
        const auto steps = elapsed->ticks / interval;
        if (steps <= 0)
            continue;

        const auto old_level = record.level;
        const auto old_suspicion = record.suspicion_micro;
        if (record.suspicion_micro > 0)
        {
            const long double total_decay = static_cast<long double>(decay) * static_cast<long double>(steps);
            record.suspicion_micro = total_decay >= static_cast<long double>(record.suspicion_micro)
                                         ? 0
                                         : record.suspicion_micro - static_cast<Fixed>(total_decay);
            if (record.suspicion_micro >= 800'000)
                record.level = AwarenessLevel::Confirmed;
            else if (record.suspicion_micro >= 400'000)
                record.level = AwarenessLevel::Aware;
            else if (record.suspicion_micro > 0)
                record.level = AwarenessLevel::Suspicious;
            else
                record.level = AwarenessLevel::Lost;
        }
        else if (record.level == AwarenessLevel::Lost)
        {
            record.level = AwarenessLevel::Unaware;
        }

        const long double delta = static_cast<long double>(steps) * static_cast<long double>(interval);
        const auto advance = delta >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())
                                 ? GameplayDuration{std::numeric_limits<std::int64_t>::max()}
                                 : GameplayDuration{static_cast<std::int64_t>(delta)};
        record.last_decay_at = SaturatingAdd(record.last_decay_at, advance);

        if (record.level == old_level && record.suspicion_micro == old_suspicion)
            continue;

        Bump();
        record.revision = revision_;
        Record({0,
                record.level == AwarenessLevel::Lost ? PerceptionChangeKind::Lost
                                                     : PerceptionChangeKind::AwarenessChanged,
                record.perceiver,
                record.target,
                {},
                {},
                record.level,
                context,
                revision_});
    }
}

bool PerceptionService::HasObservationFor(GameplayObjectRef perceiver, GameplayObjectRef target) const
{
    const auto index = observations_by_perceiver_.find(perceiver);
    if (index == observations_by_perceiver_.end())
        return false;
    for (const auto id : index->second)
    {
        const auto it = observations_.find(id);
        if (it != observations_.end() && it->second.perceived_subject == target)
            return true;
    }
    return false;
}

bool PerceptionService::HasPendingFor(GameplayObjectRef perceiver, GameplayObjectRef target) const
{
    for (const auto &[id, pending] : pending_observations_)
    {
        (void)id;
        if (pending.observation.perceiver == perceiver && pending.observation.perceived_subject == target)
            return true;
    }
    return false;
}

void PerceptionService::CleanupUnaware()
{
    std::vector<AwarenessKey> removable;
    for (const auto &[key, record] : awareness_)
    {
        if (record.level == AwarenessLevel::Unaware && !HasObservationFor(record.perceiver, record.target) &&
            !HasPendingFor(record.perceiver, record.target))
            removable.push_back(key);
    }
    std::sort(removable.begin(), removable.end());
    for (const auto &key : removable)
        awareness_.erase(key);
}

const PerceptionStimulus *PerceptionService::FindStimulus(PerceptionStimulusId id) const noexcept
{
    const auto it = stimuli_.find(id);
    return it == stimuli_.end() ? nullptr : &it->second;
}

const AwarenessRecord *PerceptionService::GetAwareness(GameplayObjectRef perceiver, GameplayObjectRef target) const noexcept
{
    const auto it = awareness_.find({perceiver, target});
    return it == awareness_.end() ? nullptr : &it->second;
}

void PerceptionService::IndexStimulus(const PerceptionStimulus &stimulus)
{
    auto &ids = stimuli_by_area_[stimulus.area];
    ids.push_back(stimulus.id);
    std::sort(ids.begin(), ids.end());
}

void PerceptionService::UnindexStimulus(const PerceptionStimulus &stimulus)
{
    const auto it = stimuli_by_area_.find(stimulus.area);
    if (it == stimuli_by_area_.end())
        return;
    auto &ids = it->second;
    ids.erase(std::remove(ids.begin(), ids.end(), stimulus.id), ids.end());
    if (ids.empty())
        stimuli_by_area_.erase(it);
}

void PerceptionService::IndexObservation(const PerceptionObservation &observation)
{
    auto &ids = observations_by_perceiver_[observation.perceiver];
    ids.push_back(observation.id);
    std::sort(ids.begin(), ids.end());
}

void PerceptionService::UnindexObservation(const PerceptionObservation &observation)
{
    const auto it = observations_by_perceiver_.find(observation.perceiver);
    if (it == observations_by_perceiver_.end())
        return;
    auto &ids = it->second;
    ids.erase(std::remove(ids.begin(), ids.end(), observation.id), ids.end());
    if (ids.empty())
        observations_by_perceiver_.erase(it);
}

std::vector<PerceptionStimulus> PerceptionService::FindStimuliInArea(GameplayObjectRef area) const
{
    std::vector<PerceptionStimulus> result;
    const auto index = stimuli_by_area_.find(area);
    if (index == stimuli_by_area_.end())
        return result;
    result.reserve(index->second.size());
    for (const auto id : index->second)
    {
        if (const auto it = stimuli_.find(id); it != stimuli_.end())
            result.push_back(it->second);
    }
    return result;
}

std::vector<PerceptionObservation> PerceptionService::FindObservationsByPerceiver(GameplayObjectRef perceiver) const
{
    std::vector<PerceptionObservation> result;
    const auto index = observations_by_perceiver_.find(perceiver);
    if (index == observations_by_perceiver_.end())
        return result;
    result.reserve(index->second.size());
    for (const auto id : index->second)
    {
        if (const auto it = observations_.find(id); it != observations_.end())
            result.push_back(it->second);
    }
    return result;
}

std::vector<PerceptionObservation> PerceptionService::FindActiveObservations(GameplayObjectRef perceiver,
                                                                            GameplayTimePoint now) const
{
    auto result = FindObservationsByPerceiver(perceiver);
    if (temporal_policy_.observation_retention.ticks <= 0)
        return result;

    result.erase(std::remove_if(result.begin(), result.end(), [&](const PerceptionObservation &observation) {
                     const auto expires_at = CheckedAdd(observation.observed_at, temporal_policy_.observation_retention);
                     return !expires_at || now >= *expires_at;
                 }),
                 result.end());
    return result;
}

PerceptionChangeBatch PerceptionService::ReadChangesSince(std::uint64_t sequence) const
{
    PerceptionChangeBatch batch;
    batch.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                       : next_change_sequence_ - 1;
    batch.oldest_available_sequence = changes_.empty()
                                          ? (next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                                        : next_change_sequence_)
                                          : changes_.front().sequence;
    if (sequence > batch.latest_sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < batch.latest_sequence;
        return batch;
    }
    if (sequence < batch.oldest_available_sequence && batch.oldest_available_sequence - sequence > 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    for (const auto &change : changes_)
    {
        if (change.sequence > sequence)
            batch.changes.push_back(change);
    }
    return batch;
}

PerceptionSnapshot PerceptionService::CaptureSnapshot() const
{
    PerceptionSnapshot snapshot;
    for (const auto &[subject, record] : perceivers_)
    {
        (void)subject;
        snapshot.perceivers.push_back(record);
    }
    for (const auto &[id, stimulus] : stimuli_)
    {
        (void)id;
        snapshot.stimuli.push_back(stimulus);
    }
    for (const auto &[id, pending] : pending_observations_)
    {
        (void)id;
        snapshot.pending_observations.push_back(pending);
    }
    for (const auto &[id, observation] : observations_)
    {
        (void)id;
        snapshot.observations.push_back(observation);
    }
    for (const auto &[key, awareness] : awareness_)
    {
        (void)key;
        snapshot.awareness.push_back(awareness);
    }

    std::sort(snapshot.perceivers.begin(), snapshot.perceivers.end(),
              [](const auto &a, const auto &b) { return a.subject < b.subject; });
    std::sort(snapshot.stimuli.begin(), snapshot.stimuli.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.pending_observations.begin(), snapshot.pending_observations.end(), [](const auto &a, const auto &b) {
        return a.ready_at == b.ready_at ? a.observation.id < b.observation.id : a.ready_at < b.ready_at;
    });
    std::sort(snapshot.observations.begin(), snapshot.observations.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.awareness.begin(), snapshot.awareness.end(), [](const auto &a, const auto &b) {
        return a.perceiver == b.perceiver ? a.target < b.target : a.perceiver < b.perceiver;
    });

    snapshot.stimulus_ids = stimulus_ids_.GetSnapshot();
    snapshot.observation_ids = observation_ids_.GetSnapshot();
    snapshot.next_change_sequence = next_change_sequence_;
    snapshot.revision = revision_;
    return snapshot;
}

bool PerceptionService::ValidGeneratorSnapshot(MonotonicIdGenerator<GameplayObjectId>::Snapshot snapshot,
                                               std::uint64_t expected_scope, std::uint64_t max_low) noexcept
{
    if (!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot) || snapshot.scope != expected_scope)
        return false;
    if (snapshot.next == 0)
        return max_low == std::numeric_limits<std::uint64_t>::max();
    return snapshot.next > max_low;
}

foundation::Result<void> PerceptionService::RestoreSnapshot(PerceptionSnapshot snapshot)
{
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.registry_not_frozen", "perception definitions must be frozen before restore"));

    decltype(perceivers_) new_perceivers;
    decltype(stimuli_) new_stimuli;
    decltype(stimuli_by_area_) new_stimuli_by_area;
    decltype(pending_observations_) new_pending;
    decltype(pending_by_ready_) new_pending_by_ready;
    decltype(observations_) new_observations;
    decltype(observations_by_perceiver_) new_observation_index;
    decltype(awareness_) new_awareness;

    std::uint64_t max_stimulus_low = 0;
    std::uint64_t max_observation_low = 0;

    for (const auto &record : snapshot.perceivers)
    {
        if (!record.subject.IsValid() || !record.profile.IsValid() || !profile_definitions_.contains(record.profile) ||
            record.revision.value > snapshot.revision.value || !new_perceivers.emplace(record.subject, record).second)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "invalid or duplicate perceiver"));
        }
    }

    for (const auto &stimulus : snapshot.stimuli)
    {
        if (!stimulus.id.IsValid() || !stimulus.source.IsValid() || !senses_.contains(stimulus.sense) ||
            stimulus.strength_micro < 0 || stimulus.strength_micro > kMicro || stimulus.lifetime.ticks < 0 ||
            stimulus.revision.value > snapshot.revision.value || !new_stimuli.emplace(stimulus.id, stimulus).second)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "invalid or duplicate stimulus"));
        }
        max_stimulus_low = std::max(max_stimulus_low, stimulus.id.value.Low());
        auto &ids = new_stimuli_by_area[stimulus.area];
        ids.push_back(stimulus.id);
    }

    std::unordered_set<PerceptionObservationId, IdHash> seen_observations;
    for (const auto &pending : snapshot.pending_observations)
    {
        const auto &observation = pending.observation;
        if (!observation.id.IsValid() || !observation.perceiver.IsValid() ||
            !new_perceivers.contains(observation.perceiver) || !senses_.contains(observation.sense) || observation.identity_confidence_micro < 0 ||
            observation.identity_confidence_micro > kMicro || observation.position_uncertainty_mm < 0 ||
            observation.revision.value > snapshot.revision.value || pending.detection_score_micro <= 0 ||
            pending.detection_score_micro > kMicro || pending.ready_at < observation.observed_at ||
            !seen_observations.insert(observation.id).second)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "invalid or duplicate pending observation"));
        }
        if (!observation.perceived_subject.IsValid() && observation.identity_confidence_micro != 0)
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "anonymous observation has identity confidence"));
        new_pending.emplace(observation.id, pending);
        if (!new_pending_by_ready.emplace(PendingKey{pending.ready_at, observation.id}, observation.id).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "duplicate pending ready key"));
        max_observation_low = std::max(max_observation_low, observation.id.value.Low());
    }

    for (const auto &observation : snapshot.observations)
    {
        if (!observation.id.IsValid() || !observation.perceiver.IsValid() ||
            !new_perceivers.contains(observation.perceiver) || !senses_.contains(observation.sense) || observation.identity_confidence_micro < 0 ||
            observation.identity_confidence_micro > kMicro || observation.position_uncertainty_mm < 0 ||
            observation.revision.value > snapshot.revision.value || !seen_observations.insert(observation.id).second)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "invalid or duplicate observation"));
        }
        if (!observation.perceived_subject.IsValid() && observation.identity_confidence_micro != 0)
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "anonymous observation has identity confidence"));
        new_observations.emplace(observation.id, observation);
        new_observation_index[observation.perceiver].push_back(observation.id);
        max_observation_low = std::max(max_observation_low, observation.id.value.Low());
    }

    for (const auto &record : snapshot.awareness)
    {
        if (!record.perceiver.IsValid() || !record.target.IsValid() || !new_perceivers.contains(record.perceiver) ||
            record.suspicion_micro < 0 || record.suspicion_micro > kMicro ||
            record.revision.value > snapshot.revision.value ||
            !new_awareness.emplace(AwarenessKey{record.perceiver, record.target}, record).second)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "invalid or duplicate awareness"));
        }
    }

    for (auto &[area, ids] : new_stimuli_by_area)
    {
        (void)area;
        std::sort(ids.begin(), ids.end());
    }
    for (auto &[subject, ids] : new_observation_index)
    {
        (void)subject;
        std::sort(ids.begin(), ids.end());
    }

    const auto current_stimulus_scope = stimulus_ids_.GetSnapshot().scope;
    const auto current_observation_scope = observation_ids_.GetSnapshot().scope;
    if (!ValidGeneratorSnapshot(snapshot.stimulus_ids, current_stimulus_scope, max_stimulus_low) ||
        !ValidGeneratorSnapshot(snapshot.observation_ids, current_observation_scope, max_observation_low))
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.restore_invalid", "invalid id generator snapshot"));
    }

    perceivers_.swap(new_perceivers);
    stimuli_.swap(new_stimuli);
    stimuli_by_area_.swap(new_stimuli_by_area);
    pending_observations_.swap(new_pending);
    pending_by_ready_.swap(new_pending_by_ready);
    observations_.swap(new_observations);
    observations_by_perceiver_.swap(new_observation_index);
    awareness_.swap(new_awareness);
    stimulus_ids_.Restore(snapshot.stimulus_ids);
    observation_ids_.Restore(snapshot.observation_ids);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = snapshot.next_change_sequence;
    tick_budget_ = {};
    return foundation::Result<void>::Success();
}

PerceptionDiagnostics PerceptionService::GetDiagnostics() const noexcept
{
    auto diagnostics = diagnostics_;
    diagnostics.perceivers = perceivers_.size();
    diagnostics.stimuli = stimuli_.size();
    diagnostics.pending_observations = pending_observations_.size();
    diagnostics.observations = observations_.size();
    diagnostics.awareness_records = awareness_.size();
    return diagnostics;
}

void PerceptionService::EnsureBudgetEpoch(GameplayTickId tick) noexcept
{
    if (tick_budget_.tick != tick)
    {
        tick_budget_ = {};
        tick_budget_.tick = tick;
    }
}

void PerceptionService::Record(PerceptionChange change)
{
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(change));
    while (changes_.size() > change_capacity_)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::perception
