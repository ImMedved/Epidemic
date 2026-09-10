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

[[nodiscard]] bool IsValidSenseEvaluationModel(SenseEvaluationModel value) noexcept
{
    switch (value)
    {
    case SenseEvaluationModel::Vision:
    case SenseEvaluationModel::Hearing:
    case SenseEvaluationModel::Radial:
    case SenseEvaluationModel::Custom:
        return true;
    }
    return false;
}

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

bool PerceptionService::CanAdvanceRevision(std::size_t count) const noexcept
{
    if (count == 0) return true;
    const auto remaining = std::numeric_limits<std::uint64_t>::max() - revision_.value;
    return count <= remaining;
}

bool PerceptionService::CanRecordChanges(std::size_t count) const noexcept
{
    if (count == 0) return true;
    if (next_change_sequence_ == 0) return false;
    const auto remaining = std::numeric_limits<std::uint64_t>::max() - next_change_sequence_;
    return count - 1 <= remaining;
}

foundation::Result<void> PerceptionService::RegisterSense(SenseDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.registry_frozen", "perception registry frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty() || senses_.contains(d.id) || !IsValidSenseEvaluationModel(d.evaluation_model) || d.base_range_mm < 0 ||
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
        return foundation::Result<void>::Failure(Error("gameplay.perception.registry_not_frozen", "perception definitions must be frozen"));
    if (!subject.IsValid() || !profile.IsValid() || !profile_definitions_.contains(profile) || perceivers_.contains(subject))
        return foundation::Result<void>::Failure(Error("gameplay.perception.invalid_perceiver", "invalid, duplicate or unknown perceiver"));
    if (!CanAdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.perception.revision_exhausted", "perception revision is exhausted"));
    if (!CanRecordChanges())
        return foundation::Result<void>::Failure(Error("gameplay.perception.change_sequence_exhausted", "perception change sequence is exhausted"));
    const Revision next{revision_.value + 1};
    bool inserted=false;
    try
    {
        perceivers_.emplace(subject, PerceiverRecord{subject, profile, next}); inserted=true;
        Record({0, PerceptionChangeKind::PerceiverRegistered, subject, {}, {}, {}, AwarenessLevel::Unaware, context, next});
    }
    catch (...)
    {
        if (inserted) perceivers_.erase(subject);
        return foundation::Result<void>::Failure(Error("gameplay.perception.publication_failed", "perceiver publication failed"));
    }
    revision_=next;
    return foundation::Result<void>::Success();
}

foundation::Result<void> PerceptionService::UnregisterPerceiver(GameplayObjectRef subject, GameplayContext context)
{
    const auto it = perceivers_.find(subject);
    if (it == perceivers_.end())
        return foundation::Result<void>::Failure(Error("gameplay.perception.perceiver_missing", "perceiver missing"));
    if (!CanAdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.perception.revision_exhausted", "perception revision is exhausted"));
    if (!CanRecordChanges())
        return foundation::Result<void>::Failure(Error("gameplay.perception.change_sequence_exhausted", "perception change sequence is exhausted"));
    const Revision next{revision_.value + 1};
    try
    {
        Record({0, PerceptionChangeKind::PerceiverUnregistered, subject, {}, {}, {}, AwarenessLevel::Unaware, context, next});
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.perception.publication_failed", "perceiver removal journal publication failed"));
    }

    std::vector<PerceptionObservationId> observation_ids;
    if (const auto index = observations_by_perceiver_.find(subject); index != observations_by_perceiver_.end())
        observation_ids = index->second;
    for (const auto id : observation_ids)
    {
        const auto oi = observations_.find(id);
        if (oi != observations_.end()) { UnindexObservation(oi->second); observations_.erase(oi); }
    }
    for (auto pi=pending_observations_.begin(); pi!=pending_observations_.end(); )
    {
        if (pi->second.observation.perceiver == subject)
        {
            pending_by_ready_.erase(PendingKey{pi->second.ready_at, pi->first});
            pi = pending_observations_.erase(pi);
        }
        else ++pi;
    }
    for (auto ai=awareness_.begin(); ai!=awareness_.end(); )
        ai = ai->second.perceiver == subject ? awareness_.erase(ai) : std::next(ai);
    perceivers_.erase(it);
    revision_=next;
    return foundation::Result<void>::Success();
}

foundation::Result<PerceptionStimulusId> PerceptionService::CreateStimulus(PerceptionStimulus s)
{
    if (!frozen_)
        return foundation::Result<PerceptionStimulusId>::Failure(Error("gameplay.perception.registry_not_frozen", "perception definitions must be frozen"));
    if (!s.sense.IsValid() || !senses_.contains(s.sense) || !s.source.IsValid() || s.strength_micro < 0 ||
        s.strength_micro > kMicro || s.lifetime.ticks < 0)
        return foundation::Result<PerceptionStimulusId>::Failure(Error("gameplay.perception.invalid_stimulus", "invalid stimulus"));

    auto staged_ids=stimulus_ids_;
    if (!s.id.IsValid())
    {
        const auto generated=staged_ids.Next();
        if (!generated.IsValid()) return foundation::Result<PerceptionStimulusId>::Failure(Error("gameplay.perception.id_exhausted", "stimulus id generator exhausted"));
        s.id=PerceptionStimulusId{generated};
    }
    if (stimuli_.contains(s.id)) return foundation::Result<PerceptionStimulusId>::Failure(Error("gameplay.perception.duplicate_stimulus", "duplicate stimulus"));
    auto gs=staged_ids.GetSnapshot();
    if (s.id.value.High()==gs.scope && gs.next!=0 && s.id.value.Low()>=gs.next)
    {
        gs.next=s.id.value.Low()==std::numeric_limits<std::uint64_t>::max()?0:s.id.value.Low()+1; staged_ids.Restore(gs);
    }
    if(!CanAdvanceRevision()) return foundation::Result<PerceptionStimulusId>::Failure(Error("gameplay.perception.revision_exhausted","perception revision is exhausted"));
    if(!CanRecordChanges()) return foundation::Result<PerceptionStimulusId>::Failure(Error("gameplay.perception.change_sequence_exhausted","perception change sequence is exhausted"));
    const Revision next{revision_.value+1}; s.revision=next; const auto id=s.id; bool inserted=false;
    try { stimuli_.emplace(id,s); inserted=true; IndexStimulus(s); Record({0,PerceptionChangeKind::StimulusCreated,s.source,{},id,{},AwarenessLevel::Unaware,s.context,next}); }
    catch(...) { if(inserted){UnindexStimulus(s);stimuli_.erase(id);} return foundation::Result<PerceptionStimulusId>::Failure(Error("gameplay.perception.publication_failed","stimulus publication failed")); }
    stimulus_ids_.Restore(staged_ids.GetSnapshot()); revision_=next; return foundation::Result<PerceptionStimulusId>::Success(id);
}

foundation::Result<void> PerceptionService::ExpireStimuli(GameplayTimePoint now, GameplayContext context)
{
    std::vector<PerceptionStimulusId> expired;
    try { expired.reserve(stimuli_.size()); }
    catch (...) { return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed", "stimulus expiry staging failed")); }
    for (const auto &[id, stimulus] : stimuli_)
    {
        if (stimulus.lifetime.ticks <= 0) continue;
        const auto expires_at = SaturatingAdd(stimulus.created_at, stimulus.lifetime);
        if (now >= expires_at) expired.push_back(id);
    }
    std::sort(expired.begin(), expired.end());
    if (expired.empty()) return foundation::Result<void>::Success();
    if (!CanAdvanceRevision(expired.size()))
        return foundation::Result<void>::Failure(Error("gameplay.perception.revision_exhausted", "perception revision is exhausted"));
    if (!CanRecordChanges(expired.size()))
        return foundation::Result<void>::Failure(Error("gameplay.perception.change_sequence_exhausted", "perception change sequence is exhausted"));

    std::deque<PerceptionChange> staged_changes;
    try { staged_changes = changes_; }
    catch (...) { return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed", "stimulus expiry journal staging failed")); }
    auto sequence = next_change_sequence_;
    auto rev = revision_.value;
    try
    {
        for (const auto id : expired)
        {
            const auto it=stimuli_.find(id); if(it==stimuli_.end()) continue;
            ++rev;
            PerceptionChange change{0,PerceptionChangeKind::StimulusExpired,it->second.source,{},id,{},AwarenessLevel::Unaware,
                                    EffectiveContext(context,it->second.context,now),Revision{rev}};
            change.sequence=sequence; staged_changes.push_back(std::move(change));
            sequence=sequence==std::numeric_limits<std::uint64_t>::max()?0:sequence+1;
            while(staged_changes.size()>change_capacity_) staged_changes.pop_front();
        }
    }
    catch (...) { return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed", "stimulus expiry journal staging failed")); }

    for (const auto id : expired)
    {
        const auto it=stimuli_.find(id); if(it==stimuli_.end()) continue;
        const auto copy=it->second; UnindexStimulus(copy); stimuli_.erase(it);
    }
    changes_.swap(staged_changes); next_change_sequence_=sequence; revision_=Revision{rev};
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

    std::unordered_map<GameplayObjectRef, const PerceiverEvaluationSample *, RefHash> sample_index;
    try { sample_index.reserve(pc.perceivers.size()); }
    catch (...) { return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.allocation_failed", "processing sample index allocation failed")); }
    for (const auto &sample : pc.perceivers)
    {
        if (!sample.subject.IsValid())
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.invalid_processing_sample", "processing sample subject is invalid"));
        if (!sample_index.emplace(sample.subject, &sample).second)
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.duplicate_processing_sample", "duplicate perceiver processing sample"));
    }

    const auto *stimulus = FindStimulus(sid);
    if (!stimulus) return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.stimulus_missing", "stimulus missing"));
    const auto *definition = FindSense(stimulus->sense);
    if (!definition) return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.invalid_sense_link", "stimulus references an unavailable sense definition"));
    if (definition->evaluation_model == SenseEvaluationModel::Custom)
    {
        const auto evaluator = evaluators_.find(definition->evaluator);
        if (evaluator == evaluators_.end() || !evaluator->second)
        {
            ++diagnostics_.evaluator_failures;
            return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.evaluator_missing", "custom sense evaluator missing"));
        }
    }

    PerceptionTickBudgetState staged_budget = tick_budget_.tick == pc.tick ? tick_budget_ : PerceptionTickBudgetState{pc.tick,0,0};
    const auto event_context = EffectiveContext(pc.gameplay, stimulus->context, pc.now);
    if (staged_budget.processed_stimuli >= budget_.max_stimuli_per_tick)
    {
        ++diagnostics_.budget_exhaustions; ++diagnostics_.dropped_stimuli;
        return foundation::Result<std::vector<PerceptionObservation>>::Success({});
    }

    struct Candidate { PerceiverRecord record{}; const PerceiverProfileDefinition *profile=nullptr; const PerceiverEvaluationSample *sample=nullptr; };
    std::vector<Candidate> candidates;
    try { candidates.reserve(std::min<std::size_t>(perceivers_.size(), budget_.max_perceivers_per_stimulus)); }
    catch (...) { return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.allocation_failed", "candidate allocation failed")); }
    for (const auto &[subject, record] : perceivers_)
    {
        if (subject == stimulus->source) continue;
        const auto *profile=FindProfileDefinition(record.profile);
        if(!profile) return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.invalid_profile_link","perceiver references an unavailable profile"));
        if(!std::binary_search(profile->senses.begin(),profile->senses.end(),stimulus->sense)) continue;
        const auto si=sample_index.find(record.subject); const auto *sample=si==sample_index.end()?nullptr:si->second;
        if(!CanAttemptSenseEvaluation(*definition,*profile,sample)) continue;
        candidates.push_back({record,profile,sample});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.record.subject<b.record.subject;});
    bool truncated=candidates.size()>budget_.max_perceivers_per_stimulus;
    if(truncated) candidates.resize(budget_.max_perceivers_per_stimulus);

    struct Prepared { PerceiverRecord record{}; SenseEvaluationResult eval{}; PerceptionObservation observation{}; GameplayTimePoint ready_at{}; Fixed score=0; bool delayed=false; };
    std::vector<Prepared> prepared;
    try { prepared.reserve(candidates.size()); }
    catch (...) { return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.allocation_failed","prepared observation allocation failed")); }
    std::uint64_t local_detection=0, local_visibility=0, local_audibility=0, local_custom=0;
    bool detection_budget_hit=false;
    for(const auto &candidate:candidates)
    {
        if(staged_budget.detection_tests>=budget_.max_detection_tests_per_tick){detection_budget_hit=true;break;}
        ++staged_budget.detection_tests; ++local_detection;
        switch(definition->evaluation_model){case SenseEvaluationModel::Vision:++local_visibility;break;case SenseEvaluationModel::Hearing:++local_audibility;break;case SenseEvaluationModel::Custom:++local_custom;break;case SenseEvaluationModel::Radial:break;}
        auto evaluated=EvaluateSense(*definition,*candidate.profile,candidate.record,*stimulus,candidate.sample,pc);
        if(!evaluated) return foundation::Result<std::vector<PerceptionObservation>>::Failure(evaluated.GetError());
        if(evaluated.Value().score_micro<=0) continue;
        Prepared item; item.record=candidate.record; item.eval=evaluated.Value(); item.score=item.eval.score_micro;
        item.ready_at=SaturatingAdd(pc.now,definition->reaction_delay); item.delayed=definition->reaction_delay.ticks>0 && item.ready_at>pc.now;
        prepared.push_back(std::move(item));
    }

    const std::size_t immediate_count=std::count_if(prepared.begin(),prepared.end(),[](const Prepared&p){return !p.delayed;});
    const std::size_t identified_immediate=std::count_if(prepared.begin(),prepared.end(),[](const Prepared&p){return !p.delayed&&p.eval.identified_subject;});
    const std::size_t revisions_needed=prepared.size()+immediate_count;
    const std::size_t changes_needed=immediate_count+identified_immediate;
    if(!CanAdvanceRevision(revisions_needed)) return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.revision_exhausted","perception revision is exhausted"));
    if(!CanRecordChanges(changes_needed)) return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.change_sequence_exhausted","perception change sequence is exhausted"));

    auto staged_ids=observation_ids_; std::uint64_t rev_cursor=revision_.value;
    for(auto &item:prepared)
    {
        const auto raw=staged_ids.Next(); if(!raw.IsValid()) return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.id_exhausted","observation id generator exhausted"));
        ++rev_cursor;
        auto &o=item.observation; o.id=PerceptionObservationId{raw}; o.perceiver=item.record.subject;
        o.perceived_subject=item.eval.identified_subject?stimulus->source:GameplayObjectRef{}; o.stimulus=sid; o.sense=stimulus->sense;
        o.confidence=ConfidenceFromScore(item.eval.score_micro); o.identity_confidence_micro=item.eval.identified_subject?ClampMicro(item.eval.identity_confidence_micro):0;
        o.perceived_position=item.eval.perceived_position; o.position_uncertainty_mm=std::max<Fixed>(0,item.eval.position_uncertainty_mm);
        o.observed_at=pc.now; o.observed_tags=stimulus->tags; o.context=event_context; o.revision=Revision{rev_cursor};
        if(!item.delayed){++rev_cursor; o.revision=Revision{rev_cursor};}
    }

    std::deque<PerceptionChange> staged_changes;
    try { staged_changes=changes_; }
    catch (...) { return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.allocation_failed","journal staging failed")); }
    auto staged_sequence=next_change_sequence_; std::uint64_t dropped=0;
    auto append_change=[&](PerceptionChange c){ c.sequence=staged_sequence; staged_changes.push_back(std::move(c)); staged_sequence=staged_sequence==std::numeric_limits<std::uint64_t>::max()?0:staged_sequence+1; while(staged_changes.size()>change_capacity_){staged_changes.pop_front();++dropped;} };

    std::unordered_map<PerceptionObservationId,PendingPerceptionObservation,IdHash> pending_nodes;
    std::map<PendingKey,PerceptionObservationId> pending_index_nodes;
    std::unordered_map<PerceptionObservationId,PerceptionObservation,IdHash> observation_nodes;
    std::unordered_map<GameplayObjectRef,std::vector<PerceptionObservationId>,RefHash> replacement_indexes;
    std::unordered_map<AwarenessKey,AwarenessRecord,AwarenessKeyHash> awareness_updates;
    std::vector<PerceptionObservation> activated; activated.reserve(immediate_count);
    try
    {
        pending_nodes.reserve(prepared.size()-immediate_count); observation_nodes.reserve(immediate_count);
        replacement_indexes.reserve(immediate_count); awareness_updates.reserve(identified_immediate);
        for(auto &item:prepared)
        {
            if(item.delayed)
            {
                PendingPerceptionObservation po{item.observation,item.ready_at,item.score};
                pending_nodes.emplace(item.observation.id,po); pending_index_nodes.emplace(PendingKey{item.ready_at,item.observation.id},item.observation.id);
                continue;
            }
            observation_nodes.emplace(item.observation.id,item.observation);
            auto [idx_it,idx_new]=replacement_indexes.try_emplace(item.observation.perceiver);
            if(idx_new){ if(auto live=observations_by_perceiver_.find(item.observation.perceiver);live!=observations_by_perceiver_.end()) idx_it->second=live->second; }
            idx_it->second.push_back(item.observation.id); std::sort(idx_it->second.begin(),idx_it->second.end());
            AwarenessLevel level=AwarenessLevel::Unaware;
            if(item.observation.perceived_subject.IsValid())
            {
                const AwarenessKey key{item.observation.perceiver,item.observation.perceived_subject};
                auto [ai, fresh]=awareness_updates.try_emplace(key);
                if(fresh){if(auto live=awareness_.find(key);live!=awareness_.end())ai->second=live->second;else ai->second=AwarenessRecord{item.observation.perceiver,item.observation.perceived_subject};}
                auto &a=ai->second; a.suspicion_micro=std::min<Fixed>(kMicro,a.suspicion_micro+std::max<Fixed>(1,item.score/2));
                a.level=a.suspicion_micro>=800'000?AwarenessLevel::Confirmed:(a.suspicion_micro>=400'000?AwarenessLevel::Aware:AwarenessLevel::Suspicious);
                a.last_observed_at=item.observation.observed_at;a.last_decay_at=item.observation.observed_at;a.last_known_position=item.observation.perceived_position;a.revision=item.observation.revision;level=a.level;
            }
            append_change({0,PerceptionChangeKind::Observed,item.observation.perceiver,item.observation.perceived_subject,sid,item.observation.id,level,event_context,item.observation.revision});
            if(item.observation.perceived_subject.IsValid()) append_change({0,PerceptionChangeKind::AwarenessChanged,item.observation.perceiver,item.observation.perceived_subject,sid,item.observation.id,level,event_context,item.observation.revision});
            activated.push_back(item.observation);
        }
        observations_.reserve(observations_.size()+observation_nodes.size()); pending_observations_.reserve(pending_observations_.size()+pending_nodes.size());
        observations_by_perceiver_.reserve(observations_by_perceiver_.size()+replacement_indexes.size()); awareness_.reserve(awareness_.size()+awareness_updates.size());
    }
    catch(...){return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.allocation_failed","perception batch staging failed"));}

    for(auto it=pending_nodes.begin();it!=pending_nodes.end();){auto node=pending_nodes.extract(it++);pending_observations_.insert(std::move(node));}
    for(auto it=pending_index_nodes.begin();it!=pending_index_nodes.end();){auto node=pending_index_nodes.extract(it++);pending_by_ready_.insert(std::move(node));}
    for(auto it=observation_nodes.begin();it!=observation_nodes.end();){auto node=observation_nodes.extract(it++);observations_.insert(std::move(node));}
    for(auto &[subject,ids]:replacement_indexes){auto live=observations_by_perceiver_.find(subject);if(live==observations_by_perceiver_.end())observations_by_perceiver_.emplace(subject,std::move(ids));else live->second.swap(ids);}
    for(auto &[key,value]:awareness_updates){auto live=awareness_.find(key);if(live==awareness_.end())awareness_.emplace(key,value);else live->second=value;}
    changes_.swap(staged_changes); next_change_sequence_=staged_sequence; observation_ids_.Restore(staged_ids.GetSnapshot()); revision_=Revision{rev_cursor};
    ++staged_budget.processed_stimuli; tick_budget_=staged_budget;
    ++diagnostics_.processed_stimuli; diagnostics_.detection_tests+=local_detection; diagnostics_.visibility_tests+=local_visibility; diagnostics_.audibility_tests+=local_audibility; diagnostics_.custom_tests+=local_custom;
    if(truncated||detection_budget_hit)++diagnostics_.budget_exhaustions;
    (void)dropped;
    return foundation::Result<std::vector<PerceptionObservation>>::Success(std::move(activated));
}

foundation::Result<void> PerceptionService::ActivateObservation(PendingPerceptionObservation pending,
                                                                GameplayContext context,
                                                                std::vector<PerceptionObservation> *activated)
{
    auto observation = std::move(pending.observation);
    if (observations_.contains(observation.id))
        return foundation::Result<void>::Failure(Error("gameplay.perception.duplicate_observation", "duplicate observation"));
    const std::size_t change_count = observation.perceived_subject.IsValid() ? 2 : 1;
    if (!CanAdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.perception.revision_exhausted", "perception revision is exhausted"));
    if (!CanRecordChanges(change_count))
        return foundation::Result<void>::Failure(Error("gameplay.perception.change_sequence_exhausted", "perception change sequence is exhausted"));
    const Revision next{revision_.value+1}; observation.revision=next;

    AwarenessRecord next_awareness; bool has_awareness=false; AwarenessLevel level=AwarenessLevel::Unaware; AwarenessKey awareness_key{};
    if(observation.perceived_subject.IsValid())
    {
        has_awareness=true; awareness_key={observation.perceiver,observation.perceived_subject};
        if(auto it=awareness_.find(awareness_key);it!=awareness_.end())next_awareness=it->second;
        else next_awareness=AwarenessRecord{observation.perceiver,observation.perceived_subject};
        next_awareness.suspicion_micro=std::min<Fixed>(kMicro,next_awareness.suspicion_micro+std::max<Fixed>(1,pending.detection_score_micro/2));
        next_awareness.level=next_awareness.suspicion_micro>=800'000?AwarenessLevel::Confirmed:(next_awareness.suspicion_micro>=400'000?AwarenessLevel::Aware:AwarenessLevel::Suspicious);
        next_awareness.last_observed_at=observation.observed_at; next_awareness.last_decay_at=observation.observed_at;
        next_awareness.last_known_position=observation.perceived_position; next_awareness.revision=next; level=next_awareness.level;
    }
    const auto event_context=context.tick.IsValid()||context.operation.IsValid()?context:observation.context;

    std::deque<PerceptionChange> staged_changes;
    std::vector<PerceptionObservationId> staged_index;
    std::unordered_map<PerceptionObservationId,PerceptionObservation,IdHash> observation_node;
    std::unordered_map<AwarenessKey,AwarenessRecord,AwarenessKeyHash> awareness_node;
    try
    {
        staged_changes=changes_; auto seq=next_change_sequence_;
        auto add=[&](PerceptionChange c){c.sequence=seq;staged_changes.push_back(std::move(c));seq=seq==std::numeric_limits<std::uint64_t>::max()?0:seq+1;while(staged_changes.size()>change_capacity_)staged_changes.pop_front();};
        add({0,PerceptionChangeKind::Observed,observation.perceiver,observation.perceived_subject,observation.stimulus,observation.id,level,event_context,next});
        if(has_awareness)add({0,PerceptionChangeKind::AwarenessChanged,observation.perceiver,observation.perceived_subject,observation.stimulus,observation.id,level,event_context,next});
        if(auto idx=observations_by_perceiver_.find(observation.perceiver);idx!=observations_by_perceiver_.end())staged_index=idx->second;
        staged_index.push_back(observation.id);std::sort(staged_index.begin(),staged_index.end());
        observation_node.emplace(observation.id,observation);
        if(has_awareness && !awareness_.contains(awareness_key))awareness_node.emplace(awareness_key,next_awareness);
        observations_.reserve(observations_.size()+1); observations_by_perceiver_.reserve(observations_by_perceiver_.size()+1); awareness_.reserve(awareness_.size()+(awareness_node.empty()?0:1));
        if(activated) activated->reserve(activated->size()+1);
    }
    catch(...){return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed","observation activation staging failed"));}

    auto on=observation_node.extract(observation.id); observations_.insert(std::move(on));
    auto idx=observations_by_perceiver_.find(observation.perceiver);
    if(idx==observations_by_perceiver_.end()) observations_by_perceiver_.emplace(observation.perceiver,std::move(staged_index)); else idx->second.swap(staged_index);
    if(has_awareness){auto ai=awareness_.find(awareness_key);if(ai==awareness_.end()){auto node=awareness_node.extract(awareness_key);awareness_.insert(std::move(node));}else ai->second=next_awareness;}
    const auto old_seq=next_change_sequence_; (void)old_seq;
    // staged change builder consumed exactly change_count sequences
    auto seq=next_change_sequence_; for(std::size_t i=0;i<change_count;++i)seq=seq==std::numeric_limits<std::uint64_t>::max()?0:seq+1;
    changes_.swap(staged_changes); next_change_sequence_=seq; revision_=next;
    if(activated) activated->push_back(observation);
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<PerceptionObservation>> PerceptionService::AdvanceTime(GameplayTimePoint now,
                                                                                       GameplayContext context)
{
    std::vector<PerceptionObservationId> due;
    try
    {
        for(auto it=pending_by_ready_.begin();it!=pending_by_ready_.end()&&it->first.ready_at<=now;++it) due.push_back(it->second);
        std::sort(due.begin(),due.end());
    }
    catch(...){return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.allocation_failed","temporal staging failed"));}
    // Conservative preflight prevents a later lifecycle step from exhausting revisions after a prefix committed.
    const std::size_t upper_revisions = due.size() + observations_.size() + awareness_.size();
    const std::size_t upper_changes = due.size()*2 + observations_.size() + awareness_.size();
    if(!CanAdvanceRevision(upper_revisions)) return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.revision_exhausted","perception revision is exhausted"));
    if(!CanRecordChanges(upper_changes)) return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.change_sequence_exhausted","perception change sequence is exhausted"));

    std::vector<PerceptionObservation> activated;
    try { activated.reserve(due.size()); }
    catch(...){return foundation::Result<std::vector<PerceptionObservation>>::Failure(Error("gameplay.perception.allocation_failed","activation output staging failed"));}
    for(const auto id:due)
    {
        const auto pending_it=pending_observations_.find(id); if(pending_it==pending_observations_.end())continue;
        auto result=ActivateObservation(pending_it->second,context,&activated);
        if(!result)return foundation::Result<std::vector<PerceptionObservation>>::Failure(result.GetError());
        pending_by_ready_.erase(PendingKey{pending_it->second.ready_at,id}); pending_observations_.erase(pending_it);
    }
    auto expired=ExpireObservations(now,context); if(!expired)return foundation::Result<std::vector<PerceptionObservation>>::Failure(expired.GetError());
    auto decayed=DecayAwareness(now,context); if(!decayed)return foundation::Result<std::vector<PerceptionObservation>>::Failure(decayed.GetError());
    CleanupUnaware();
    return foundation::Result<std::vector<PerceptionObservation>>::Success(std::move(activated));
}

foundation::Result<void> PerceptionService::ExpireObservations(GameplayTimePoint now, GameplayContext context)
{
    if (temporal_policy_.observation_retention.ticks <= 0) return foundation::Result<void>::Success();
    std::vector<PerceptionObservationId> expired;
    try
    {
        expired.reserve(observations_.size());
        for(const auto &[id,observation]:observations_){const auto expires_at=CheckedAdd(observation.observed_at,temporal_policy_.observation_retention);if(!expires_at||now>=*expires_at)expired.push_back(id);}
        std::sort(expired.begin(),expired.end());
    }
    catch(...){return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed","observation expiry staging failed"));}
    if(expired.empty())return foundation::Result<void>::Success();
    if(!CanAdvanceRevision(expired.size()))return foundation::Result<void>::Failure(Error("gameplay.perception.revision_exhausted","perception revision is exhausted"));
    if(!CanRecordChanges(expired.size()))return foundation::Result<void>::Failure(Error("gameplay.perception.change_sequence_exhausted","perception change sequence is exhausted"));
    std::deque<PerceptionChange> staged; try{staged=changes_;}catch(...){return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed","observation expiry journal staging failed"));}
    auto seq=next_change_sequence_;auto rev=revision_.value;
    try{for(const auto id:expired){const auto it=observations_.find(id);if(it==observations_.end())continue;++rev;PerceptionChange c{0,PerceptionChangeKind::Lost,it->second.perceiver,it->second.perceived_subject,it->second.stimulus,id,AwarenessLevel::Unaware,EffectiveContext(context,it->second.context,now),Revision{rev}};c.sequence=seq;staged.push_back(std::move(c));seq=seq==std::numeric_limits<std::uint64_t>::max()?0:seq+1;while(staged.size()>change_capacity_)staged.pop_front();}}
    catch(...){return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed","observation expiry journal staging failed"));}
    for(const auto id:expired){auto it=observations_.find(id);if(it!=observations_.end()){const auto copy=it->second;UnindexObservation(copy);observations_.erase(it);}}
    changes_.swap(staged);next_change_sequence_=seq;revision_=Revision{rev};return foundation::Result<void>::Success();
}

foundation::Result<void> PerceptionService::DecayAwareness(GameplayTimePoint now, GameplayContext context)
{
    const auto interval = temporal_policy_.awareness_decay_interval.ticks;
    const auto decay = temporal_policy_.awareness_decay_micro_per_interval;
    if (interval <= 0 || decay <= 0)
        return foundation::Result<void>::Success();

    struct Update
    {
        AwarenessKey key{};
        AwarenessRecord value{};
        PerceptionChangeKind kind = PerceptionChangeKind::AwarenessChanged;
    };
    std::vector<Update> updates;
    try
    {
        updates.reserve(awareness_.size());
        for (const auto &[key, current] : awareness_)
        {
            auto value = current;
            if (now <= value.last_decay_at)
                continue;
            const auto elapsed = CheckedDifference(now, value.last_decay_at);
            if (!elapsed || elapsed->ticks <= 0)
                continue;
            const auto steps = elapsed->ticks / interval;
            if (steps <= 0)
                continue;

            const auto old_level = value.level;
            const auto old_suspicion = value.suspicion_micro;
            if (value.suspicion_micro > 0)
            {
                const long double total_decay = static_cast<long double>(decay) * static_cast<long double>(steps);
                value.suspicion_micro = total_decay >= static_cast<long double>(value.suspicion_micro)
                                             ? 0
                                             : value.suspicion_micro - static_cast<Fixed>(total_decay);
                if (value.suspicion_micro >= 800'000)
                    value.level = AwarenessLevel::Confirmed;
                else if (value.suspicion_micro >= 400'000)
                    value.level = AwarenessLevel::Aware;
                else if (value.suspicion_micro > 0)
                    value.level = AwarenessLevel::Suspicious;
                else
                    value.level = AwarenessLevel::Lost;
            }
            else if (value.level == AwarenessLevel::Lost)
            {
                value.level = AwarenessLevel::Unaware;
            }

            const long double delta = static_cast<long double>(steps) * static_cast<long double>(interval);
            const auto advance = delta >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())
                                     ? GameplayDuration{std::numeric_limits<std::int64_t>::max()}
                                     : GameplayDuration{static_cast<std::int64_t>(delta)};
            value.last_decay_at = SaturatingAdd(value.last_decay_at, advance);
            if (value.level == old_level && value.suspicion_micro == old_suspicion)
                continue;
            updates.push_back(Update{key, value,
                                     value.level == AwarenessLevel::Lost ? PerceptionChangeKind::Lost
                                                                        : PerceptionChangeKind::AwarenessChanged});
        }
        std::sort(updates.begin(), updates.end(), [](const Update &a, const Update &b) { return a.key < b.key; });
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed", "awareness decay staging failed"));
    }

    if (updates.empty())
        return foundation::Result<void>::Success();
    if (!CanAdvanceRevision(updates.size()))
        return foundation::Result<void>::Failure(Error("gameplay.perception.revision_exhausted", "perception revision is exhausted"));
    if (!CanRecordChanges(updates.size()))
        return foundation::Result<void>::Failure(Error("gameplay.perception.change_sequence_exhausted", "perception change sequence is exhausted"));

    std::deque<PerceptionChange> staged;
    try { staged = changes_; }
    catch (...) { return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed", "awareness journal staging failed")); }
    auto seq = next_change_sequence_;
    auto rev = revision_.value;
    try
    {
        for (auto &update : updates)
        {
            ++rev;
            update.value.revision = Revision{rev};
            PerceptionChange change{0, update.kind, update.value.perceiver, update.value.target, {}, {},
                                    update.value.level, EffectiveContext(context, {}, now), update.value.revision};
            change.sequence = seq;
            staged.push_back(std::move(change));
            seq = seq == std::numeric_limits<std::uint64_t>::max() ? 0 : seq + 1;
            while (staged.size() > change_capacity_) staged.pop_front();
        }
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.perception.allocation_failed", "awareness journal staging failed"));
    }

    for (const auto &update : updates)
    {
        auto it = awareness_.find(update.key);
        if (it != awareness_.end())
            it->second = update.value;
    }
    changes_.swap(staged);
    next_change_sequence_ = seq;
    revision_ = Revision{rev};
    return foundation::Result<void>::Success();
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
    removable.reserve(awareness_.size());
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

PerceptionChangeBatch PerceptionService::ReadChangesSinceSequence(std::uint64_t sequence) const
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
    snapshot.change_epoch = journal_epoch_;
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
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
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
    journal_epoch_ = *next_journal_epoch;
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
    changes_.push_back(std::move(change));
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    while (changes_.size() > change_capacity_)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::perception
