#include "Epidemic/GameFramework/NeedsLife/needs_life.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace epidemic::gameplay::needs_life
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m) { return foundation::Error::Create(c, m); }

constexpr std::int64_t kMicro = 1'000'000;

bool ValidMicro(std::int64_t value) noexcept { return value >= 0 && value <= kMicro; }

bool AdvanceGeneratorPastRequested(MonotonicIdGenerator<GameplayObjectId> &generator, GameplayObjectId id) noexcept
{
    if (!id.IsValid() || id.High() != generator.Scope().Raw())
        return true;
    auto snapshot = generator.GetSnapshot();
    if (snapshot.next == 0 || id.Low() < snapshot.next)
        return true;
    snapshot.next = id.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.Low() + 1;
    generator.Restore(snapshot);
    return true;
}
} // namespace

std::int64_t NeedsLifeService::SaturatingAdd64(std::int64_t a, std::int64_t b) noexcept
{
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
        return std::numeric_limits<std::int64_t>::max();
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)
        return std::numeric_limits<std::int64_t>::min();
    return a + b;
}

std::int64_t NeedsLifeService::SaturatingMultiply64(std::int64_t a, std::int64_t b) noexcept
{
    if (a == 0 || b == 0)
        return 0;
    if ((a == -1 && b == std::numeric_limits<std::int64_t>::min()) ||
        (b == -1 && a == std::numeric_limits<std::int64_t>::min()))
        return std::numeric_limits<std::int64_t>::max();
    if (a > 0)
    {
        if (b > 0 && a > std::numeric_limits<std::int64_t>::max() / b)
            return std::numeric_limits<std::int64_t>::max();
        if (b < 0 && b < std::numeric_limits<std::int64_t>::min() / a)
            return std::numeric_limits<std::int64_t>::min();
    }
    else
    {
        if (b > 0 && a < std::numeric_limits<std::int64_t>::min() / b)
            return std::numeric_limits<std::int64_t>::min();
        if (b < 0 && a < std::numeric_limits<std::int64_t>::max() / b)
            return std::numeric_limits<std::int64_t>::max();
    }
    return a * b;
}

std::int64_t NeedsLifeService::ScaleMicro(std::int64_t value, std::int64_t multiplier_micro) noexcept
{
    const auto product = SaturatingMultiply64(value, multiplier_micro);
    return product / kMicro;
}

bool NeedsLifeService::AdvanceRevision() noexcept
{
    const auto next = CheckedNext(revision_);
    if (!next)
        return false;
    revision_ = *next;
    return true;
}

foundation::Result<void> NeedsLifeService::RegisterDecayRule(NeedDecayRule rule)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.needs.definitions_frozen", "definitions are frozen"));
    if (!rule.id.IsValid() || rule.rate_multiplier_micro < 0)
        return foundation::Result<void>::Failure(Error("gameplay.needs.invalid_decay_rule", "invalid decay rule"));
    if (decay_rules_.contains(rule.id))
        return foundation::Result<void>::Failure(Error("gameplay.needs.duplicate_decay_rule", "duplicate decay rule"));
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    rule.revision = revision_;
    decay_rules_.emplace(rule.id, std::move(rule));
    return foundation::Result<void>::Success();
}

foundation::Result<void> NeedsLifeService::RegisterSimulationProfile(LifeSimulationProfile profile)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.needs.definitions_frozen", "definitions are frozen"));
    if (!profile.id.IsValid() || profile.abstract_rate_multiplier_micro < 0 ||
        profile.materialized_rate_multiplier_micro < 0)
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.invalid_simulation_profile", "invalid simulation profile"));
    if (simulation_profiles_.contains(profile.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.duplicate_simulation_profile", "duplicate simulation profile"));
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    profile.revision = revision_;
    simulation_profiles_.emplace(profile.id, std::move(profile));
    return foundation::Result<void>::Success();
}

foundation::Result<void> NeedsLifeService::ValidateDefinition(const NeedDefinition &d) const
{
    if (!d.id.IsValid() || d.min_value > d.max_value || d.decay_per_tick < 0)
        return foundation::Result<void>::Failure(Error("gameplay.needs.invalid_definition", "invalid need definition"));
    if (d.decay_rule.IsValid() && !decay_rules_.contains(d.decay_rule))
        return foundation::Result<void>::Failure(Error("gameplay.needs.unknown_decay_rule", "unknown decay rule"));
    if (!ValidMicro(d.satisfied_threshold_micro) || !ValidMicro(d.low_threshold_micro) ||
        !ValidMicro(d.medium_threshold_micro) || !ValidMicro(d.high_threshold_micro) ||
        d.satisfied_threshold_micro < d.low_threshold_micro || d.low_threshold_micro < d.medium_threshold_micro ||
        d.medium_threshold_micro < d.high_threshold_micro)
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.invalid_thresholds", "invalid need threshold policy"));
    return foundation::Result<void>::Success();
}

foundation::Result<void> NeedsLifeService::RegisterNeedDefinition(NeedDefinition d)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.needs.definitions_frozen", "definitions are frozen"));
    if (auto valid = ValidateDefinition(d); !valid)
        return valid;
    if (definitions_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.duplicate_definition", "duplicate need definition"));
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    d.default_value = Clamp(d, d.default_value);
    d.revision = revision_;
    definitions_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}

foundation::Result<void> NeedsLifeService::FreezeDefinitions()
{
    if (definitions_frozen_)
        return foundation::Result<void>::Success();
    for (const auto &[id, def] : definitions_)
    {
        (void)id;
        if (auto valid = ValidateDefinition(def); !valid)
            return valid;
    }
    definitions_frozen_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<void> NeedsLifeService::ValidateProfile(const NeedProfile &p) const
{
    if (!p.subject.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.needs.invalid_profile", "invalid need profile"));
    if (p.simulation_profile.IsValid() && !simulation_profiles_.contains(p.simulation_profile))
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.unknown_simulation_profile", "unknown simulation profile"));
    std::unordered_set<NeedTypeId, IdHash> unique;
    for (auto n : p.active_needs)
    {
        if (!n.IsValid() || !definitions_.contains(n))
            return foundation::Result<void>::Failure(Error("gameplay.needs.unknown_need", "unknown need type"));
        if (!unique.insert(n).second)
            return foundation::Result<void>::Failure(Error("gameplay.needs.duplicate_need", "duplicate active need"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<NeedProfileId> NeedsLifeService::CreateNeedProfile(NeedProfile p, GameplayContext c)
{
    if (auto valid = ValidateProfile(p); !valid)
        return foundation::Result<NeedProfileId>::Failure(valid.GetError());
    if (profile_by_subject_.contains(p.subject))
        return foundation::Result<NeedProfileId>::Failure(
            Error("gameplay.needs.profile_exists", "subject already has an active need profile"));
    if (!p.id.IsValid())
        p.id = NeedProfileId{profile_ids_.Next()};
    if (!p.id.IsValid())
        return foundation::Result<NeedProfileId>::Failure(Error("gameplay.needs.id_exhausted", "profile id exhausted"));
    if (profiles_.contains(p.id))
        return foundation::Result<NeedProfileId>::Failure(
            Error("gameplay.needs.duplicate_profile", "duplicate need profile"));
    AdvanceGeneratorPastRequested(profile_ids_, p.id.value);
    std::sort(p.active_needs.begin(), p.active_needs.end());
    if (p.last_simulated_at.ticks == 0)
        p.last_simulated_at = c.time;
    if (!AdvanceRevision())
        return foundation::Result<NeedProfileId>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    p.revision = revision_;
    for (auto n : p.active_needs)
    {
        const auto &def = definitions_.at(n);
        NeedState st{p.subject, n, def.default_value, ThresholdFor(def, def.default_value), c.time, revision_};
        states_.emplace(StateKey(p.subject, n), st);
    }
    const auto id = p.id;
    const auto subject = p.subject;
    profiles_.emplace(id, std::move(p));
    profile_by_subject_[subject] = id;
    Record({0, NeedsLifeChangeKind::NeedProfileCreated, subject, {}, {}, NeedThreshold::Satisfied, c, revision_});
    return foundation::Result<NeedProfileId>::Success(id);
}

foundation::Result<void> NeedsLifeService::RemoveNeedProfile(GameplayObjectRef subject, GameplayContext c)
{
    auto index_it = profile_by_subject_.find(subject);
    if (index_it == profile_by_subject_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.profile_missing", "need profile missing"));
    const auto profile_it = profiles_.find(index_it->second);
    if (profile_it == profiles_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.invariant", "profile index is stale"));
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    for (auto need : profile_it->second.active_needs)
        states_.erase(StateKey(subject, need));
    profiles_.erase(profile_it);
    profile_by_subject_.erase(index_it);
    Record({0, NeedsLifeChangeKind::NeedProfileChanged, subject, {}, {}, NeedThreshold::Satisfied, c, revision_});
    return foundation::Result<void>::Success();
}

std::int64_t NeedsLifeService::EffectiveDecayRate(const NeedDefinition &definition,
                                                  std::int64_t simulation_multiplier_micro) const noexcept
{
    auto rate = definition.decay_per_tick;
    if (definition.decay_rule.IsValid())
    {
        const auto it = decay_rules_.find(definition.decay_rule);
        if (it != decay_rules_.end())
            rate = ScaleMicro(rate, it->second.rate_multiplier_micro);
    }
    return ScaleMicro(rate, simulation_multiplier_micro);
}

NeedState NeedsLifeService::EvaluateNeedWithMultiplier(GameplayObjectRef s, NeedTypeId n, GameplayTimePoint now,
                                                       std::int64_t multiplier) const
{
    auto it = states_.find(StateKey(s, n));
    if (it == states_.end())
        return {};
    auto state = it->second;
    const auto def_it = definitions_.find(n);
    if (def_it == definitions_.end() || now <= state.last_updated_at)
        return state;
    const auto elapsed = (now - state.last_updated_at).ticks;
    const auto rate = EffectiveDecayRate(def_it->second, multiplier);
    const auto delta = SaturatingMultiply64(elapsed, rate);
    state.value = Clamp(def_it->second, SaturatingAdd64(state.value, -delta));
    state.threshold = ThresholdFor(def_it->second, state.value);
    return state;
}

NeedState NeedsLifeService::EvaluateNeed(GameplayObjectRef s, NeedTypeId n, GameplayTimePoint now) const
{
    return EvaluateNeedWithMultiplier(s, n, now, kMicro);
}

foundation::Result<void> NeedsLifeService::CommitNeedEvaluation(GameplayObjectRef s, NeedTypeId n,
                                                                GameplayTimePoint now, GameplayContext c)
{
    auto it = states_.find(StateKey(s, n));
    if (it == states_.end() || !definitions_.contains(n))
        return foundation::Result<void>::Failure(Error("gameplay.needs.state_missing", "need state missing"));
    if (now < it->second.last_updated_at)
        return foundation::Result<void>::Failure(Error("gameplay.needs.time_regression", "need time regressed"));
    auto evaluated = EvaluateNeed(s, n, now);
    const auto previous = it->second.threshold;
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    evaluated.last_updated_at = now;
    evaluated.revision = revision_;
    it->second = evaluated;
    Record({0, NeedsLifeChangeKind::NeedChanged, s, n, {}, evaluated.threshold, c, revision_});
    if (previous != evaluated.threshold)
    {
        ++diagnostics_.threshold_events;
        Record({0, NeedsLifeChangeKind::NeedThresholdCrossed, s, n, {}, evaluated.threshold, c, revision_});
    }
    // If all active need states now share this time, the profile simulation cursor can safely follow.
    if (auto profile = profile_by_subject_.find(s); profile != profile_by_subject_.end())
    {
        auto &p = profiles_.at(profile->second);
        bool all_at_now = true;
        for (auto need : p.active_needs)
        {
            const auto state_it = states_.find(StateKey(s, need));
            if (state_it == states_.end() || state_it->second.last_updated_at != now)
            {
                all_at_now = false;
                break;
            }
        }
        if (all_at_now)
        {
            p.last_simulated_at = now;
            p.revision = revision_;
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> NeedsLifeService::SatisfyNeed(SatisfyNeedRequest r)
{
    if (!r.subject.IsValid() || !definitions_.contains(r.need) || r.amount < 0)
        return foundation::Result<void>::Failure(Error("gameplay.needs.invalid_satisfy", "invalid satisfy request"));
    auto it = states_.find(StateKey(r.subject, r.need));
    if (it == states_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.state_missing", "need state missing"));
    if (r.context.time < it->second.last_updated_at)
        return foundation::Result<void>::Failure(Error("gameplay.needs.time_regression", "need time regressed"));
    const auto &def = definitions_.at(r.need);
    auto state = EvaluateNeed(r.subject, r.need, r.context.time);
    const auto previous = state.threshold;
    state.value = Clamp(def, SaturatingAdd64(state.value, r.amount));
    state.threshold = ThresholdFor(def, state.value);
    state.last_updated_at = r.context.time;
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    state.revision = revision_;
    it->second = state;
    Record({0, NeedsLifeChangeKind::NeedSatisfied, r.subject, r.need, {}, state.threshold, r.context, revision_});
    if (previous != state.threshold)
    {
        ++diagnostics_.threshold_events;
        Record({0, NeedsLifeChangeKind::NeedThresholdCrossed, r.subject, r.need, {}, state.threshold, r.context, revision_});
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> NeedsLifeService::AddNeedPressure(AddNeedPressureRequest r)
{
    if (!r.subject.IsValid() || !definitions_.contains(r.need) || r.amount < 0)
        return foundation::Result<void>::Failure(Error("gameplay.needs.invalid_pressure_delta", "invalid need pressure"));
    auto it = states_.find(StateKey(r.subject, r.need));
    if (it == states_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.state_missing", "need state missing"));
    if (r.context.time < it->second.last_updated_at)
        return foundation::Result<void>::Failure(Error("gameplay.needs.time_regression", "need time regressed"));
    const auto &def = definitions_.at(r.need);
    auto state = EvaluateNeed(r.subject, r.need, r.context.time);
    const auto previous = state.threshold;
    state.value = Clamp(def, SaturatingAdd64(state.value, -r.amount));
    state.threshold = ThresholdFor(def, state.value);
    state.last_updated_at = r.context.time;
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    state.revision = revision_;
    it->second = state;
    Record({0, NeedsLifeChangeKind::NeedPressureAdded, r.subject, r.need, {}, state.threshold, r.context, revision_});
    if (previous != state.threshold)
    {
        ++diagnostics_.threshold_events;
        Record({0, NeedsLifeChangeKind::NeedThresholdCrossed, r.subject, r.need, {}, state.threshold, r.context, revision_});
    }
    return foundation::Result<void>::Success();
}

foundation::Result<LifePressureId> NeedsLifeService::CreateLifePressure(LifePressure p, GameplayContext c)
{
    if (!p.subject.IsValid() || !p.type.IsValid() || p.urgency < 0 ||
        (p.expires_at && *p.expires_at <= c.time))
        return foundation::Result<LifePressureId>::Failure(Error("gameplay.needs.invalid_pressure", "invalid pressure"));
    if (!p.id.IsValid())
        p.id = LifePressureId{pressure_ids_.Next()};
    if (!p.id.IsValid())
        return foundation::Result<LifePressureId>::Failure(Error("gameplay.needs.id_exhausted", "pressure id exhausted"));
    if (pressures_.contains(p.id))
        return foundation::Result<LifePressureId>::Failure(
            Error("gameplay.needs.duplicate_pressure", "duplicate pressure"));
    AdvanceGeneratorPastRequested(pressure_ids_, p.id.value);
    if (!AdvanceRevision())
        return foundation::Result<LifePressureId>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    p.state = LifePressureState::Active;
    p.revision = revision_;
    const auto id = p.id;
    const auto subject = p.subject;
    pressures_.emplace(id, p);
    pressures_by_subject_.emplace(subject, id);
    Record({0, NeedsLifeChangeKind::LifePressureCreated, subject, {}, id, NeedThreshold::Satisfied, c, revision_});
    return foundation::Result<LifePressureId>::Success(id);
}

foundation::Result<void> NeedsLifeService::ResolveLifePressure(LifePressureId id, GameplayContext c)
{
    auto it = pressures_.find(id);
    if (it == pressures_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.pressure_missing", "pressure missing"));
    if (it->second.state != LifePressureState::Active)
        return foundation::Result<void>::Success();
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    it->second.state = LifePressureState::Resolved;
    it->second.revision = revision_;
    Record({0, NeedsLifeChangeKind::LifePressureResolved, it->second.subject, {}, id, NeedThreshold::Satisfied, c,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<std::size_t> NeedsLifeService::SweepExpiredPressures(GameplayTimePoint now, GameplayContext c)
{
    std::vector<LifePressureId> due;
    for (const auto &[id, pressure] : pressures_)
        if (pressure.state == LifePressureState::Active && pressure.expires_at && *pressure.expires_at <= now)
            due.push_back(id);
    std::sort(due.begin(), due.end());
    for (auto id : due)
    {
        if (!AdvanceRevision())
            return foundation::Result<std::size_t>::Failure(
                Error("gameplay.needs.revision_exhausted", "revision exhausted"));
        auto &pressure = pressures_.at(id);
        pressure.state = LifePressureState::Expired;
        pressure.revision = revision_;
        auto event_context = c;
        event_context.time = now;
        Record({0, NeedsLifeChangeKind::LifePressureExpired, pressure.subject, {}, id, NeedThreshold::Satisfied,
                event_context, revision_});
    }
    diagnostics_.expired_pressures += due.size();
    return foundation::Result<std::size_t>::Success(due.size());
}

foundation::Result<LifeRoutineId> NeedsLifeService::SetRoutine(LifeRoutine r, GameplayContext c)
{
    if (!r.subject.IsValid())
        return foundation::Result<LifeRoutineId>::Failure(Error("gameplay.needs.invalid_routine", "invalid routine"));
    for (const auto &entry : r.entries)
        if (!entry.routine_type.IsValid() || entry.duration.ticks <= 0)
            return foundation::Result<LifeRoutineId>::Failure(Error("gameplay.needs.invalid_routine", "invalid routine entry"));
    std::sort(r.entries.begin(), r.entries.end(), [](const auto &a, const auto &b) {
        if (a.start != b.start)
            return a.start < b.start;
        return a.routine_type < b.routine_type;
    });
    auto existing = routine_by_subject_.find(r.subject);
    if (existing != routine_by_subject_.end())
    {
        if (r.id.IsValid() && r.id != existing->second)
            return foundation::Result<LifeRoutineId>::Failure(
                Error("gameplay.needs.routine_exists", "subject already has a different routine"));
        r.id = existing->second;
    }
    else if (!r.id.IsValid())
    {
        r.id = LifeRoutineId{routine_ids_.Next()};
    }
    if (!r.id.IsValid())
        return foundation::Result<LifeRoutineId>::Failure(Error("gameplay.needs.id_exhausted", "routine id exhausted"));
    AdvanceGeneratorPastRequested(routine_ids_, r.id.value);
    if (!AdvanceRevision())
        return foundation::Result<LifeRoutineId>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    r.revision = revision_;
    const auto id = r.id;
    const auto subject = r.subject;
    routines_[id] = std::move(r);
    routine_by_subject_[subject] = id;
    Record({0, NeedsLifeChangeKind::RoutineChanged, subject, {}, {}, NeedThreshold::Satisfied, c, revision_});
    return foundation::Result<LifeRoutineId>::Success(id);
}

foundation::Result<void> NeedsLifeService::RemoveRoutine(GameplayObjectRef subject, GameplayContext c)
{
    auto it = routine_by_subject_.find(subject);
    if (it == routine_by_subject_.end())
        return foundation::Result<void>::Success();
    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    routines_.erase(it->second);
    routine_by_subject_.erase(it);
    Record({0, NeedsLifeChangeKind::RoutineChanged, subject, {}, {}, NeedThreshold::Satisfied, c, revision_});
    return foundation::Result<void>::Success();
}

std::vector<LifeRoutineOccurrence> NeedsLifeService::FindRoutineEntriesInInterval(GameplayObjectRef subject,
                                                                                  GameplayTimePoint from,
                                                                                  GameplayTimePoint to) const
{
    std::vector<LifeRoutineOccurrence> out;
    const auto *routine = GetRoutine(subject);
    if (!routine || to <= from)
        return out;
    for (std::uint32_t i = 0; i < routine->entries.size(); ++i)
    {
        const auto &entry = routine->entries[i];
        if (entry.start > from && entry.start <= to)
            out.push_back({routine->id, i, entry.start, entry});
    }
    return out;
}

std::size_t NeedsLifeService::PruneTerminalPressures(GameplayObjectRef subject) noexcept
{
    std::vector<LifePressureId> remove;
    for (const auto &[id, pressure] : pressures_)
        if (pressure.state != LifePressureState::Active && (!subject.IsValid() || pressure.subject == subject))
            remove.push_back(id);
    for (auto id : remove)
        pressures_.erase(id);
    if (!remove.empty())
        RebuildIndexes();
    return remove.size();
}

foundation::Result<void> NeedsLifeService::SimulateLifeInterval(LifeSimulationRequest r)
{
    if (!r.subject.IsValid() || r.to <= r.from)
        return foundation::Result<void>::Failure(Error("gameplay.needs.invalid_simulation", "invalid simulation request"));
    auto profile_idx = profile_by_subject_.find(r.subject);
    if (profile_idx == profile_by_subject_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.profile_missing", "need profile missing"));
    auto profile_it = profiles_.find(profile_idx->second);
    if (profile_it == profiles_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.invariant", "profile index is stale"));
    const auto &profile = profile_it->second;
    if (r.from != profile.last_simulated_at)
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.interval_conflict", "simulation interval does not start at profile cursor"));
    if (profile.materialization_policy == NeedMaterializationPolicy::MaterializedOnly && !r.materialized)
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.materialization_required", "profile requires materialized simulation"));

    std::int64_t multiplier = kMicro;
    if (profile.simulation_profile.IsValid())
    {
        const auto policy = simulation_profiles_.find(profile.simulation_profile);
        if (policy == simulation_profiles_.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.needs.unknown_simulation_profile", "unknown simulation profile"));
        multiplier = r.materialized ? policy->second.materialized_rate_multiplier_micro
                                    : policy->second.abstract_rate_multiplier_micro;
    }
    const bool disabled_abstract = profile.materialization_policy == NeedMaterializationPolicy::DisabledWhenAbstract &&
                                   !r.materialized;
    if (disabled_abstract)
        multiplier = 0;

    struct Staged
    {
        NeedStateKey key{};
        NeedState state{};
        NeedThreshold previous{};
    };
    std::vector<Staged> staged;
    staged.reserve(profile.active_needs.size());
    for (auto need : profile.active_needs)
    {
        auto state_it = states_.find(StateKey(r.subject, need));
        if (state_it == states_.end())
            return foundation::Result<void>::Failure(Error("gameplay.needs.state_missing", "need state missing"));
        // Strict interval simulation owns the interval. A state ahead of from indicates another mutation/simulation path.
        if (state_it->second.last_updated_at > r.from)
            return foundation::Result<void>::Failure(
                Error("gameplay.needs.interval_conflict", "need state is ahead of simulation interval"));
        auto evaluated = EvaluateNeedWithMultiplier(r.subject, need, r.to, multiplier);
        evaluated.last_updated_at = r.to;
        staged.push_back({StateKey(r.subject, need), evaluated, state_it->second.threshold});
    }

    if (!AdvanceRevision())
        return foundation::Result<void>::Failure(Error("gameplay.needs.revision_exhausted", "revision exhausted"));
    auto event_context = r.context;
    event_context.time = r.to;
    for (auto &item : staged)
    {
        item.state.revision = revision_;
        states_[item.key] = item.state;
        Record({0, NeedsLifeChangeKind::NeedChanged, r.subject, item.key.need, {}, item.state.threshold, event_context,
                revision_});
        if (item.previous != item.state.threshold)
        {
            ++diagnostics_.threshold_events;
            Record({0, NeedsLifeChangeKind::NeedThresholdCrossed, r.subject, item.key.need, {}, item.state.threshold,
                    event_context, revision_});
        }
    }
    profile_it->second.last_simulated_at = r.to;
    profile_it->second.revision = revision_;
    if (r.materialized)
        ++diagnostics_.materialized_simulation_steps;
    else
        ++diagnostics_.abstract_simulation_steps;
    Record({0, NeedsLifeChangeKind::LifeSimulationStepCompleted, r.subject, {}, {}, NeedThreshold::Satisfied,
            event_context, revision_});
    return foundation::Result<void>::Success();
}

const NeedProfile *NeedsLifeService::GetNeedProfile(GameplayObjectRef s) const noexcept
{
    const auto index = profile_by_subject_.find(s);
    if (index == profile_by_subject_.end())
        return nullptr;
    const auto it = profiles_.find(index->second);
    return it == profiles_.end() ? nullptr : &it->second;
}

const NeedState *NeedsLifeService::GetCommittedNeedState(GameplayObjectRef s, NeedTypeId n) const noexcept
{
    const auto it = states_.find(StateKey(s, n));
    return it == states_.end() ? nullptr : &it->second;
}

std::int64_t NeedsLifeService::GetNeedUrgency(GameplayObjectRef s, NeedTypeId n, GameplayTimePoint now) const
{
    const auto def = definitions_.find(n);
    if (def == definitions_.end())
        return 0;
    const auto state = EvaluateNeed(s, n, now);
    return SaturatingAdd64(def->second.max_value, -state.value);
}

std::vector<NeedState> NeedsLifeService::FindCriticalNeeds(GameplayTimePoint now) const
{
    std::vector<NeedState> out;
    for (const auto &[key, st] : states_)
    {
        (void)key;
        auto evaluated = EvaluateNeed(st.subject, st.type, now);
        if (evaluated.threshold == NeedThreshold::Critical)
            out.push_back(evaluated);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.subject != b.subject)
            return a.subject < b.subject;
        return a.type < b.type;
    });
    return out;
}

std::vector<NeedState> NeedsLifeService::FindCriticalNeeds(GameplayObjectRef subject, GameplayTimePoint now) const
{
    std::vector<NeedState> out;
    const auto *profile = GetNeedProfile(subject);
    if (!profile)
        return out;
    for (auto need : profile->active_needs)
    {
        auto evaluated = EvaluateNeed(subject, need, now);
        if (evaluated.threshold == NeedThreshold::Critical)
            out.push_back(evaluated);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.type < b.type; });
    return out;
}

std::vector<LifePressure> NeedsLifeService::FindLifePressures(GameplayObjectRef s) const
{
    std::vector<LifePressure> out;
    const auto range = pressures_by_subject_.equal_range(s);
    for (auto it = range.first; it != range.second; ++it)
    {
        const auto pressure = pressures_.find(it->second);
        if (pressure != pressures_.end() && pressure->second.state == LifePressureState::Active)
            out.push_back(pressure->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.urgency != b.urgency)
            return a.urgency > b.urgency;
        return a.id < b.id;
    });
    return out;
}

const LifeRoutine *NeedsLifeService::GetRoutine(GameplayObjectRef s) const noexcept
{
    const auto index = routine_by_subject_.find(s);
    if (index == routine_by_subject_.end())
        return nullptr;
    const auto it = routines_.find(index->second);
    return it == routines_.end() ? nullptr : &it->second;
}

NeedsLifeChangeBatch NeedsLifeService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    NeedsLifeChangeBatch batch;
    batch.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                       : next_change_sequence_ - 1;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (next_change_sequence_ == 0 || sequence > batch.latest_sequence)
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
        if (change.sequence > sequence)
            batch.changes.push_back(change);
    return batch;
}

std::vector<NeedsLifeChange> NeedsLifeService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

void NeedsLifeService::PruneChangesThrough(std::uint64_t sequence) noexcept
{
    while (!changes_.empty() && changes_.front().sequence <= sequence)
        changes_.pop_front();
}

void NeedsLifeService::SetChangeJournalCapacity(std::size_t capacity) noexcept
{
    change_journal_capacity_ = std::max<std::size_t>(1, capacity);
    TrimJournal();
}

NeedsLifeSnapshot NeedsLifeService::CaptureSnapshot() const
{
    NeedsLifeSnapshot s;
    for (const auto &[id, value] : decay_rules_)
    {
        (void)id;
        s.decay_rules.push_back(value);
    }
    for (const auto &[id, value] : simulation_profiles_)
    {
        (void)id;
        s.simulation_profiles.push_back(value);
    }
    for (const auto &[id, value] : definitions_)
    {
        (void)id;
        s.definitions.push_back(value);
    }
    for (const auto &[id, value] : profiles_)
    {
        (void)id;
        s.profiles.push_back(value);
    }
    for (const auto &[key, value] : states_)
    {
        (void)key;
        s.states.push_back(value);
    }
    for (const auto &[id, value] : pressures_)
    {
        (void)id;
        s.pressures.push_back(value);
    }
    for (const auto &[id, value] : routines_)
    {
        (void)id;
        s.routines.push_back(value);
    }
    std::sort(s.decay_rules.begin(), s.decay_rules.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.simulation_profiles.begin(), s.simulation_profiles.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.definitions.begin(), s.definitions.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.profiles.begin(), s.profiles.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.states.begin(), s.states.end(), [](const auto &a, const auto &b) {
        if (a.subject != b.subject)
            return a.subject < b.subject;
        return a.type < b.type;
    });
    std::sort(s.pressures.begin(), s.pressures.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.routines.begin(), s.routines.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    s.profile_ids = profile_ids_.GetSnapshot();
    s.pressure_ids = pressure_ids_.GetSnapshot();
    s.routine_ids = routine_ids_.GetSnapshot();
    s.revision = revision_;
    s.change_epoch = journal_epoch_;
    return s;
}

foundation::Result<void> NeedsLifeService::RestoreSnapshot(NeedsLifeSnapshot s)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(s.change_epoch > journal_epoch_ ? s.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    decltype(decay_rules_) new_decay_rules;
    decltype(simulation_profiles_) new_simulation_profiles;
    decltype(definitions_) new_definitions;
    decltype(profiles_) new_profiles;
    decltype(profile_by_subject_) new_profile_by_subject;
    decltype(states_) new_states;
    decltype(pressures_) new_pressures;
    decltype(pressures_by_subject_) new_pressures_by_subject;
    decltype(routines_) new_routines;
    decltype(routine_by_subject_) new_routine_by_subject;

    for (auto &rule : s.decay_rules)
    {
        if (!rule.id.IsValid() || rule.rate_multiplier_micro < 0 || rule.revision > s.revision ||
            !new_decay_rules.emplace(rule.id, rule).second)
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid decay rule"));
    }
    for (auto &profile : s.simulation_profiles)
    {
        if (!profile.id.IsValid() || profile.abstract_rate_multiplier_micro < 0 ||
            profile.materialized_rate_multiplier_micro < 0 || profile.revision > s.revision ||
            !new_simulation_profiles.emplace(profile.id, profile).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.needs.restore_invalid", "invalid simulation profile"));
    }
    for (auto &def : s.definitions)
    {
        if (!def.id.IsValid() || def.min_value > def.max_value || def.decay_per_tick < 0 || def.revision > s.revision ||
            (def.decay_rule.IsValid() && !new_decay_rules.contains(def.decay_rule)) ||
            !ValidMicro(def.satisfied_threshold_micro) || !ValidMicro(def.low_threshold_micro) ||
            !ValidMicro(def.medium_threshold_micro) || !ValidMicro(def.high_threshold_micro) ||
            def.satisfied_threshold_micro < def.low_threshold_micro || def.low_threshold_micro < def.medium_threshold_micro ||
            def.medium_threshold_micro < def.high_threshold_micro || !new_definitions.emplace(def.id, def).second)
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid need definition"));
    }

    std::uint64_t max_profile = 0, max_pressure = 0, max_routine = 0;
    for (auto &profile : s.profiles)
    {
        if (!profile.id.IsValid() || !profile.subject.IsValid() || profile.revision > s.revision ||
            (profile.simulation_profile.IsValid() && !new_simulation_profiles.contains(profile.simulation_profile)) ||
            new_profile_by_subject.contains(profile.subject))
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid need profile"));
        std::sort(profile.active_needs.begin(), profile.active_needs.end());
        if (std::adjacent_find(profile.active_needs.begin(), profile.active_needs.end()) != profile.active_needs.end())
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "duplicate active need"));
        for (auto need : profile.active_needs)
            if (!new_definitions.contains(need))
                return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "unknown profile need"));
        if (!new_profiles.emplace(profile.id, profile).second)
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "duplicate profile id"));
        new_profile_by_subject.emplace(profile.subject, profile.id);
        if (profile.id.value.High() == s.profile_ids.scope)
            max_profile = std::max(max_profile, profile.id.value.Low());
    }
    for (auto &state : s.states)
    {
        const auto profile_index = new_profile_by_subject.find(state.subject);
        if (!state.subject.IsValid() || !state.type.IsValid() || state.revision > s.revision ||
            profile_index == new_profile_by_subject.end() || !new_definitions.contains(state.type))
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid need state"));
        const auto &profile = new_profiles.at(profile_index->second);
        if (!std::binary_search(profile.active_needs.begin(), profile.active_needs.end(), state.type))
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "state not active in profile"));
        const auto &def = new_definitions.at(state.type);
        if (state.value < def.min_value || state.value > def.max_value || state.threshold != ThresholdFor(def, state.value) ||
            !new_states.emplace(StateKey(state.subject, state.type), state).second)
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid need state"));
    }
    for (const auto &[id, profile] : new_profiles)
    {
        (void)id;
        for (auto need : profile.active_needs)
            if (!new_states.contains(StateKey(profile.subject, need)))
                return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "missing need state"));
    }
    for (auto &pressure : s.pressures)
    {
        if (!pressure.id.IsValid() || !pressure.subject.IsValid() || !pressure.type.IsValid() || pressure.urgency < 0 ||
            pressure.revision > s.revision || !new_pressures.emplace(pressure.id, pressure).second)
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid pressure"));
        new_pressures_by_subject.emplace(pressure.subject, pressure.id);
        if (pressure.id.value.High() == s.pressure_ids.scope)
            max_pressure = std::max(max_pressure, pressure.id.value.Low());
    }
    for (auto &routine : s.routines)
    {
        if (!routine.id.IsValid() || !routine.subject.IsValid() || routine.revision > s.revision ||
            new_routine_by_subject.contains(routine.subject))
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid routine"));
        for (const auto &entry : routine.entries)
            if (!entry.routine_type.IsValid() || entry.duration.ticks <= 0)
                return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid routine entry"));
        if (!new_routines.emplace(routine.id, routine).second)
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "duplicate routine id"));
        new_routine_by_subject.emplace(routine.subject, routine.id);
        if (routine.id.value.High() == s.routine_ids.scope)
            max_routine = std::max(max_routine, routine.id.value.Low());
    }

    const auto pgen = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.profile_ids, profile_ids_.Scope(), max_profile);
    const auto lgen = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.pressure_ids, pressure_ids_.Scope(), max_pressure);
    const auto rgen = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.routine_ids, routine_ids_.Scope(), max_routine);
    if (!pgen || !lgen || !rgen)
        return foundation::Result<void>::Failure(Error("gameplay.needs.restore_generator_invalid", "invalid id generator snapshot"));

    decay_rules_ = std::move(new_decay_rules);
    simulation_profiles_ = std::move(new_simulation_profiles);
    definitions_ = std::move(new_definitions);
    profiles_ = std::move(new_profiles);
    profile_by_subject_ = std::move(new_profile_by_subject);
    states_ = std::move(new_states);
    pressures_ = std::move(new_pressures);
    pressures_by_subject_ = std::move(new_pressures_by_subject);
    routines_ = std::move(new_routines);
    routine_by_subject_ = std::move(new_routine_by_subject);
    profile_ids_.Restore(s.profile_ids);
    pressure_ids_.Restore(s.pressure_ids);
    routine_ids_.Restore(s.routine_ids);
    revision_ = s.revision;
    definitions_frozen_ = true;
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_ = {};
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

NeedsLifeDiagnostics NeedsLifeService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.definitions = definitions_.size();
    d.profiles = profiles_.size();
    d.states = states_.size();
    d.routines = routines_.size();
    d.life_pressures = 0;
    d.critical_needs = 0;
    for (const auto &[id, pressure] : pressures_)
    {
        (void)id;
        if (pressure.state == LifePressureState::Active)
            ++d.life_pressures;
    }
    for (const auto &[key, state] : states_)
    {
        (void)key;
        if (state.threshold == NeedThreshold::Critical)
            ++d.critical_needs;
    }
    return d;
}

NeedThreshold NeedsLifeService::ThresholdFor(const NeedDefinition &d, std::int64_t value) const noexcept
{
    const auto span = std::max<std::int64_t>(1, SaturatingAdd64(d.max_value, -d.min_value));
    const auto offset = std::max<std::int64_t>(0, SaturatingAdd64(value, -d.min_value));
    const auto micro = std::min<std::int64_t>(kMicro, SaturatingMultiply64(offset, kMicro) / span);
    if (micro >= d.satisfied_threshold_micro)
        return NeedThreshold::Satisfied;
    if (micro >= d.low_threshold_micro)
        return NeedThreshold::Low;
    if (micro >= d.medium_threshold_micro)
        return NeedThreshold::Medium;
    if (micro >= d.high_threshold_micro)
        return NeedThreshold::High;
    return NeedThreshold::Critical;
}

NeedStateKey NeedsLifeService::StateKey(GameplayObjectRef s, NeedTypeId n) noexcept { return {s, n}; }

void NeedsLifeService::Record(NeedsLifeChange change)
{
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    const auto next = CheckedNextSequence(next_change_sequence_);
    next_change_sequence_ = next ? *next : 0;
    changes_.push_back(std::move(change));
    TrimJournal();
}

void NeedsLifeService::TrimJournal() noexcept
{
    while (changes_.size() > change_journal_capacity_)
        changes_.pop_front();
}

void NeedsLifeService::RebuildIndexes()
{
    profile_by_subject_.clear();
    pressures_by_subject_.clear();
    routine_by_subject_.clear();
    for (const auto &[id, profile] : profiles_)
        profile_by_subject_[profile.subject] = id;
    for (const auto &[id, pressure] : pressures_)
        pressures_by_subject_.emplace(pressure.subject, id);
    for (const auto &[id, routine] : routines_)
        routine_by_subject_[routine.subject] = id;
}
} // namespace epidemic::gameplay::needs_life
