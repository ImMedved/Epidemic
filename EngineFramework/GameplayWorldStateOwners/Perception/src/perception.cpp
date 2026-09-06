#include "Epidemic/GameFramework/Perception/perception.h"
#include "Epidemic/Foundation/error.h"

#include <cmath>
#include <iterator>
#include <limits>

namespace epidemic::gameplay::perception
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
PerceptionService::PerceptionService() = default;
foundation::Result<void> PerceptionService::RegisterSense(SenseDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.registry_frozen", "perception registry frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty() || senses_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.invalid_sense", "invalid or duplicate sense"));
    senses_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<void> PerceptionService::RegisterProfile(PerceiverProfile p)
{
    if (frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.registry_frozen", "perception registry frozen"));
    if (!p.id.IsValid() || !p.subject.IsValid() || profiles_.contains(p.id) || profile_by_subject_.contains(p.subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.perception.invalid_profile", "invalid or duplicate perceiver profile"));
    std::sort(p.senses.begin(), p.senses.end());
    for (auto s : p.senses)
    {
        if (!senses_.contains(s))
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.unknown_sense", "profile references unknown sense"));
    }
    Bump();
    p.revision = revision_;
    profile_by_subject_.emplace(p.subject, p.id);
    profiles_.emplace(p.id, std::move(p));
    return foundation::Result<void>::Success();
}
const SenseDefinition *PerceptionService::FindSense(SenseTypeId id) const noexcept
{
    auto it = senses_.find(id);
    return it == senses_.end() ? nullptr : &it->second;
}
const PerceiverProfile *PerceptionService::FindProfile(PerceiverProfileId id) const noexcept
{
    auto it = profiles_.find(id);
    return it == profiles_.end() ? nullptr : &it->second;
}
const PerceiverProfile *PerceptionService::FindProfileBySubject(GameplayObjectRef subject) const noexcept
{
    auto it = profile_by_subject_.find(subject);
    return it == profile_by_subject_.end() ? nullptr : FindProfile(it->second);
}
foundation::Result<PerceptionStimulusId> PerceptionService::CreateStimulus(PerceptionStimulus s)
{
    if (!s.sense.IsValid() || !senses_.contains(s.sense) || !s.source.IsValid())
        return foundation::Result<PerceptionStimulusId>::Failure(
            Error("gameplay.perception.invalid_stimulus", "invalid stimulus"));
    if (!s.id.IsValid())
        s.id = PerceptionStimulusId{stimulus_ids_.Next()};
    if (stimuli_.contains(s.id))
        return foundation::Result<PerceptionStimulusId>::Failure(
            Error("gameplay.perception.duplicate_stimulus", "duplicate stimulus"));
    Bump();
    s.revision = revision_;
    auto id = s.id;
    stimuli_.emplace(s.id, s);
    Record({0,
            PerceptionChangeKind::StimulusCreated,
            s.source,
            {},
            id,
            {},
            AwarenessLevel::Unaware,
            s.context,
            revision_});
    return foundation::Result<PerceptionStimulusId>::Success(id);
}
foundation::Result<void> PerceptionService::ExpireStimuli(GameplayTimePoint now)
{
    MaintainTemporalState(now, {});
    std::vector<PerceptionStimulusId> ids;
    for (const auto &[id, s] : stimuli_)
    {
        if (s.lifetime.ticks > 0 && now.ticks >= s.created_at.ticks + s.lifetime.ticks)
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    for (auto id : ids)
    {
        auto it = stimuli_.find(id);
        if (it != stimuli_.end())
        {
            Bump();
            Record({0,
                    PerceptionChangeKind::StimulusExpired,
                    it->second.source,
                    {},
                    id,
                    {},
                    AwarenessLevel::Unaware,
                    it->second.context,
                    revision_});
            stimuli_.erase(it);
        }
    }
    return foundation::Result<void>::Success();
}

void PerceptionService::MaintainTemporalState(GameplayTimePoint now, GameplayContext context)
{
    if (temporal_policy_.observation_retention.ticks > 0)
    {
        std::vector<PerceptionObservationId> expired;
        for (const auto &[id, observation] : observations_)
        {
            const auto age = now - observation.observed_at;
            if (age.ticks >= temporal_policy_.observation_retention.ticks)
                expired.push_back(id);
        }
        std::sort(expired.begin(), expired.end());
        for (auto id : expired)
        {
            auto it = observations_.find(id);
            if (it == observations_.end())
                continue;
            const auto observation = it->second;
            Bump();
            Record({0,
                    PerceptionChangeKind::Lost,
                    observation.perceiver,
                    observation.perceived_subject,
                    observation.stimulus,
                    observation.id,
                    AwarenessLevel::Lost,
                    context.tick.IsValid() ? context : observation.context,
                    revision_});
            observations_.erase(it);
        }
    }

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
        auto it = awareness_.find(key);
        if (it == awareness_.end())
            continue;
        auto &record = it->second;
        if (now <= record.last_decay_at)
            continue;

        const auto elapsed = now - record.last_decay_at;
        const auto steps = elapsed.ticks / interval;
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

        record.last_decay_at = record.last_decay_at + GameplayDuration{steps * interval};
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

Fixed PerceptionService::DistanceSquared(WorldPosition a, WorldPosition b) noexcept
{
    const long double dx = static_cast<long double>(a.x_mm) - static_cast<long double>(b.x_mm);
    const long double dy = static_cast<long double>(a.y_mm) - static_cast<long double>(b.y_mm);
    const long double dz = static_cast<long double>(a.z_mm) - static_cast<long double>(b.z_mm);
    const long double d = dx * dx + dy * dy + dz * dz;
    return d > static_cast<long double>(std::numeric_limits<Fixed>::max()) ? std::numeric_limits<Fixed>::max()
                                                                           : static_cast<Fixed>(d);
}
Fixed PerceptionService::ScaledRange(Fixed base_range_mm, Fixed multiplier_micro) noexcept
{
    if (base_range_mm <= 0 || multiplier_micro <= 0)
        return 0;
    const long double scaled = static_cast<long double>(base_range_mm) * static_cast<long double>(multiplier_micro) /
                               1'000'000.0L;
    if (scaled >= static_cast<long double>(std::numeric_limits<Fixed>::max()))
        return std::numeric_limits<Fixed>::max();
    return std::max<Fixed>(1, static_cast<Fixed>(scaled));
}
Fixed PerceptionService::DistanceAttenuatedScore(Fixed strength_micro, Fixed range_mm, WorldPosition observer,
                                                  WorldPosition stimulus) noexcept
{
    if (strength_micro <= 0 || range_mm <= 0)
        return 0;
    const long double dx = static_cast<long double>(observer.x_mm) - static_cast<long double>(stimulus.x_mm);
    const long double dy = static_cast<long double>(observer.y_mm) - static_cast<long double>(stimulus.y_mm);
    const long double dz = static_cast<long double>(observer.z_mm) - static_cast<long double>(stimulus.z_mm);
    const long double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const long double range = static_cast<long double>(range_mm);
    if (distance >= range)
        return 0;
    const long double attenuation = 1.0L - distance / range;
    const long double clamped_strength = static_cast<long double>(std::clamp<Fixed>(strength_micro, 0, 1'000'000));
    const long double score = clamped_strength * attenuation;
    return std::clamp<Fixed>(static_cast<Fixed>(score), 0, 1'000'000);
}
PerceptionConfidence PerceptionService::ConfidenceFromScore(Fixed score) const noexcept
{
    if (score <= 0)
        return PerceptionConfidence::None;
    if (score < 250000)
        return PerceptionConfidence::Low;
    if (score < 600000)
        return PerceptionConfidence::Medium;
    if (score < 950000)
        return PerceptionConfidence::High;
    return PerceptionConfidence::Certain;
}
Fixed PerceptionService::ModifierFor(GameplayObjectRef p, SenseTypeId s) const noexcept
{
    auto it = modifiers_.find({p, GameplayObjectRef{GameplayDomainId{s.value.Raw()}, GameplayObjectId::FromRaw(0, 1)}});
    (void)s;
    return it == modifiers_.end() ? 1'000'000 : it->second;
}
bool PerceptionService::IsOccluded(GameplayObjectRef p, GameplayObjectRef t) const noexcept
{
    auto it = occlusion_.find({p, t});
    return it != occlusion_.end() && it->second;
}
void PerceptionService::SetOccluded(GameplayObjectRef p, GameplayObjectRef t, bool o)
{
    occlusion_[{p, t}] = o;
}
void PerceptionService::SetSenseModifier(GameplayObjectRef p, SenseTypeId s, Fixed m)
{
    modifiers_[{p, GameplayObjectRef{GameplayDomainId{s.value.Raw()}, GameplayObjectId::FromRaw(0, 1)}}] = m;
}
VisibilityResult PerceptionService::EvaluateVisibility(GameplayObjectRef p, GameplayObjectRef t, WorldPosition op,
                                                       WorldPosition tp) const
{
    VisibilityResult r;
    r.revision = revision_;
    auto *prof = FindProfileBySubject(p);
    if (!prof || !t.IsValid())
    {
        r.state = PerceptionVisibilityState::Unavailable;
        return r;
    }
    auto vision = SenseTypeId::FromString("framework.sense.vision");
    SenseTypeId chosen{};
    for (auto s : prof->senses)
    {
        auto *def = FindSense(s);
        if (def && (s == vision || chosen.IsValid() == false))
            chosen = s;
    }
    auto *def = FindSense(chosen);
    if (!def || def->base_range_mm <= 0)
    {
        r.state = PerceptionVisibilityState::Unavailable;
        return r;
    }
    if (IsOccluded(p, t))
    {
        r.state = PerceptionVisibilityState::Occluded;
        r.score_micro = 0;
        return r;
    }
    auto d2 = DistanceSquared(op, tp);
    auto range = ScaledRange(def->base_range_mm, ModifierFor(p, chosen));
    auto range2 = DistanceSquared({}, WorldPosition{range, 0, 0});
    if (d2 > range2)
    {
        r.state = PerceptionVisibilityState::Hidden;
        r.score_micro = 0;
        return r;
    }
    r.score_micro = std::max<Fixed>(1, 1'000'000 - (d2 * 1'000'000 / std::max<Fixed>(1, range2)));
    r.state = r.score_micro < 400000 ? PerceptionVisibilityState::PartiallyVisible : PerceptionVisibilityState::Visible;
    return r;
}
AudibilityResult PerceptionService::EvaluateAudibility(GameplayObjectRef p, PerceptionStimulusId sid) const
{
    return EvaluateAudibility(p, sid, {});
}
AudibilityResult PerceptionService::EvaluateAudibility(GameplayObjectRef p, PerceptionStimulusId sid,
                                                        WorldPosition observer_position) const
{
    AudibilityResult r;
    r.revision = revision_;
    auto *prof = FindProfileBySubject(p);
    auto *stim = FindStimulus(sid);
    if (!prof || !stim)
    {
        r.state = PerceptionAudibilityState::Unavailable;
        return r;
    }
    bool has = false;
    Fixed range = 0;
    for (auto s : prof->senses)
    {
        auto *def = FindSense(s);
        if (def && s == stim->sense)
        {
            has = true;
            range = ScaledRange(def->base_range_mm, ModifierFor(p, s));
            break;
        }
    }
    if (!has || range <= 0)
    {
        r.state = PerceptionAudibilityState::NotHeard;
        return r;
    }
    r.score_micro = DistanceAttenuatedScore(stim->strength_micro, range, observer_position, stim->position);
    if (r.score_micro <= 0)
    {
        r.state = PerceptionAudibilityState::NotHeard;
        return r;
    }
    r.state = r.score_micro > 800000 ? PerceptionAudibilityState::HeardExactly
                                     : (r.score_micro > 350000 ? PerceptionAudibilityState::HeardDirectionOnly
                                                               : PerceptionAudibilityState::HeardVagueNoise);
    return r;
}
AwarenessRecord &PerceptionService::TouchAwareness(GameplayObjectRef p, GameplayObjectRef t)
{
    auto [it, inserted] = awareness_.try_emplace({p, t}, AwarenessRecord{p, t});
    (void)inserted;
    return it->second;
}
foundation::Result<std::vector<PerceptionObservation>> PerceptionService::ProcessStimulus(
    PerceptionStimulusId sid, const PerceptionProcessingContext &pc)
{
    EnsureBudgetEpoch(pc.tick);
    MaintainTemporalState(pc.now, pc.gameplay);
    auto *stim = FindStimulus(sid);
    if (!stim)
        return foundation::Result<std::vector<PerceptionObservation>>::Failure(
            Error("gameplay.perception.stimulus_missing", "stimulus missing"));
    const auto event_context = pc.gameplay.tick.IsValid() ? pc.gameplay : stim->context;
    if (tick_budget_.processed_stimuli >= budget_.max_stimuli_per_tick)
    {
        ++diagnostics_.budget_exhaustions;
        Record({0,
                PerceptionChangeKind::BudgetExceeded,
                stim->source,
                {},
                sid,
                {},
                AwarenessLevel::Unaware,
                event_context,
                revision_});
        return foundation::Result<std::vector<PerceptionObservation>>::Success({});
    }
    ++tick_budget_.processed_stimuli;
    ++diagnostics_.processed_stimuli;
    std::vector<PerceiverProfile> candidates;
    for (const auto &[id, p] : profiles_)
    {
        (void)id;
        if (p.subject == stim->source)
            continue;
        if (std::binary_search(p.senses.begin(), p.senses.end(), stim->sense))
            candidates.push_back(p);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.subject < b.subject; });
    if (candidates.size() > budget_.max_perceivers_per_stimulus)
        candidates.resize(budget_.max_perceivers_per_stimulus);
    std::vector<PerceptionObservation> out;
    const auto hearing = SenseTypeId::FromString("framework.sense.hearing");
    const auto vision = SenseTypeId::FromString("framework.sense.vision");
    for (const auto &p : candidates)
    {
        const auto observer_position = pc.FindPerceiverPosition(p.subject).value_or(WorldPosition{});
        Fixed detection_score = 0;
        if (stim->sense == hearing)
        {
            auto audible = EvaluateAudibility(p.subject, sid, observer_position);
            ++diagnostics_.audibility_tests;
            detection_score = audible.score_micro;
        }
        else if (stim->sense == vision)
        {
            auto visible = EvaluateVisibility(p.subject, stim->source, observer_position, stim->position);
            ++diagnostics_.visibility_tests;
            detection_score = visible.score_micro <= 0
                                  ? 0
                                  : static_cast<Fixed>((static_cast<long double>(visible.score_micro) *
                                                        std::clamp<Fixed>(stim->strength_micro, 0, 1'000'000)) /
                                                       1'000'000.0L);
        }
        else
        {
            const auto *definition = FindSense(stim->sense);
            if (definition)
            {
                const auto range = ScaledRange(definition->base_range_mm, ModifierFor(p.subject, stim->sense));
                detection_score = DistanceAttenuatedScore(stim->strength_micro, range, observer_position, stim->position);
            }
        }
        if (detection_score <= 0)
            continue;
        Bump();
        auto oid = PerceptionObservationId{observation_ids_.Next()};
        PerceptionObservation o{oid,
                                p.subject,
                                stim->source,
                                sid,
                                stim->sense,
                                ConfidenceFromScore(detection_score),
                                stim->position,
                                pc.now,
                                stim->tags,
                                event_context,
                                revision_};
        observations_.emplace(oid, o);
        auto &aw = TouchAwareness(p.subject, stim->source);
        aw.suspicion_micro = std::min<Fixed>(1'000'000, aw.suspicion_micro + detection_score / 2);
        aw.level = aw.suspicion_micro >= 800000
                       ? AwarenessLevel::Confirmed
                       : (aw.suspicion_micro >= 400000 ? AwarenessLevel::Aware : AwarenessLevel::Suspicious);
        aw.last_observed_at = pc.now;
        aw.last_decay_at = pc.now;
        aw.last_known_position = stim->position;
        aw.revision = revision_;
        Record(
            {0, PerceptionChangeKind::Observed, p.subject, stim->source, sid, oid, aw.level, event_context, revision_});
        Record({0, PerceptionChangeKind::AwarenessChanged, p.subject, stim->source, sid, oid, aw.level, event_context,
                revision_});
        out.push_back(o);
    }
    return foundation::Result<std::vector<PerceptionObservation>>::Success(out);
}
foundation::Result<std::vector<PerceptionObservation>> PerceptionService::ProcessStimulus(PerceptionStimulusId sid,
                                                                                          GameplayTimePoint now)
{
    return ProcessStimulus(sid,
                           PerceptionProcessingContext{
                               GameplayTickId{static_cast<std::uint64_t>(now.ticks > 0 ? now.ticks : 1)}, now, {}});
}
const PerceptionStimulus *PerceptionService::FindStimulus(PerceptionStimulusId id) const noexcept
{
    auto it = stimuli_.find(id);
    return it == stimuli_.end() ? nullptr : &it->second;
}
const AwarenessRecord *PerceptionService::GetAwareness(GameplayObjectRef p, GameplayObjectRef t) const noexcept
{
    auto it = awareness_.find({p, t});
    return it == awareness_.end() ? nullptr : &it->second;
}
std::vector<PerceptionStimulus> PerceptionService::FindStimuliInArea(GameplayObjectRef area) const
{
    std::vector<PerceptionStimulus> out;
    for (const auto &[id, s] : stimuli_)
    {
        (void)id;
        if (s.area == area)
            out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<PerceptionObservation> PerceptionService::FindObservationsByPerceiver(GameplayObjectRef p) const
{
    std::vector<PerceptionObservation> out;
    for (const auto &[id, o] : observations_)
    {
        (void)id;
        if (o.perceiver == p)
            out.push_back(o);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<PerceptionChange> PerceptionService::ChangesSince(std::uint64_t seq) const
{
    std::vector<PerceptionChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](const auto &c) { return c.sequence > seq; });
    return out;
}
PerceptionSnapshot PerceptionService::CaptureSnapshot() const
{
    PerceptionSnapshot s;
    for (const auto &[id, p] : profiles_)
    {
        (void)id;
        s.profiles.push_back(p);
    }
    for (const auto &[id, st] : stimuli_)
    {
        (void)id;
        s.stimuli.push_back(st);
    }
    for (const auto &[id, o] : observations_)
    {
        (void)id;
        s.observations.push_back(o);
    }
    for (const auto &[k, a] : awareness_)
    {
        (void)k;
        s.awareness.push_back(a);
    }
    std::sort(s.profiles.begin(), s.profiles.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.stimuli.begin(), s.stimuli.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.observations.begin(), s.observations.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.awareness.begin(), s.awareness.end(), [](auto &a, auto &b) {
        return a.perceiver == b.perceiver ? a.target < b.target : a.perceiver < b.perceiver;
    });
    s.stimulus_ids = stimulus_ids_.GetSnapshot();
    s.observation_ids = observation_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> PerceptionService::RestoreSnapshot(PerceptionSnapshot s)
{
    profiles_.clear();
    profile_by_subject_.clear();
    stimuli_.clear();
    observations_.clear();
    awareness_.clear();
    for (auto &p : s.profiles)
    {
        if (!p.id.IsValid() || !p.subject.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.perception.restore_invalid", "invalid profile"));
        profile_by_subject_[p.subject] = p.id;
        profiles_[p.id] = p;
    }
    for (auto &st : s.stimuli)
    {
        if (!st.id.IsValid() || !senses_.contains(st.sense))
            return foundation::Result<void>::Failure(Error("gameplay.perception.restore_invalid", "invalid stimulus"));
        stimuli_[st.id] = st;
    }
    for (auto &o : s.observations)
    {
        if (!o.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.perception.restore_invalid", "invalid observation"));
        observations_[o.id] = o;
    }
    for (auto &a : s.awareness)
    {
        if (!a.perceiver.IsValid() || !a.target.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.perception.restore_invalid", "invalid awareness"));
        awareness_[{a.perceiver, a.target}] = a;
    }
    stimulus_ids_.Restore(s.stimulus_ids);
    observation_ids_.Restore(s.observation_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
PerceptionDiagnostics PerceptionService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.profiles = profiles_.size();
    d.stimuli = stimuli_.size();
    d.observations = observations_.size();
    d.awareness_records = awareness_.size();
    return d;
}
void PerceptionService::EnsureBudgetEpoch(GameplayTickId tick) noexcept
{
    if (tick_budget_.tick != tick)
    {
        tick_budget_ = {};
        tick_budget_.tick = tick;
    }
}
void PerceptionService::Record(PerceptionChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::perception
