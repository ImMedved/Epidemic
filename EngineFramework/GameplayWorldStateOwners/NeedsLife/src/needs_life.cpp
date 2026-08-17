#include "Epidemic/GameFramework/NeedsLife/needs_life.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <iterator>
#include <utility>

namespace epidemic::gameplay::needs_life
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
foundation::Result<void> NeedsLifeService::RegisterNeedDefinition(NeedDefinition d)
{
    if (!d.id.IsValid() || d.min_value > d.max_value)
        return foundation::Result<void>::Failure(Error("gameplay.needs.invalid_definition", "invalid need definition"));
    if (definitions_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.duplicate_definition", "duplicate need definition"));
    Bump();
    d.default_value = Clamp(d, d.default_value);
    d.revision = revision_;
    definitions_.emplace(d.id, d);
    return foundation::Result<void>::Success();
}
foundation::Result<NeedProfileId> NeedsLifeService::CreateNeedProfile(NeedProfile p, GameplayContext c)
{
    if (!p.subject.IsValid())
        return foundation::Result<NeedProfileId>::Failure(
            Error("gameplay.needs.invalid_profile", "invalid need profile"));
    if (!p.id.IsValid())
        p.id = NeedProfileId{profile_ids_.Next()};
    if (profiles_.contains(p.id))
        return foundation::Result<NeedProfileId>::Failure(
            Error("gameplay.needs.duplicate_profile", "duplicate need profile"));
    for (auto n : p.active_needs)
    {
        if (!definitions_.contains(n))
            return foundation::Result<NeedProfileId>::Failure(
                Error("gameplay.needs.unknown_need", "unknown need type"));
    }
    std::sort(p.active_needs.begin(), p.active_needs.end());
    Bump();
    p.revision = revision_;
    for (auto n : p.active_needs)
    {
        const auto &def = definitions_.at(n);
        NeedState st;
        st.subject = p.subject;
        st.type = n;
        st.value = def.default_value;
        st.threshold = ThresholdFor(def, st.value);
        st.last_updated_at = c.time;
        st.revision = revision_;
        states_[StateKey(p.subject, n)] = st;
    }
    auto id = p.id;
    auto subject = p.subject;
    profiles_.emplace(id, p);
    Record({0, NeedsLifeChangeKind::NeedProfileCreated, subject, {}, {}, NeedThreshold::Satisfied, c, revision_});
    return foundation::Result<NeedProfileId>::Success(id);
}
NeedState NeedsLifeService::EvaluateNeed(GameplayObjectRef s, NeedTypeId n, GameplayTimePoint now) const
{
    auto key = StateKey(s, n);
    auto it = states_.find(key);
    if (it == states_.end())
        return {};
    auto state = it->second;
    auto def_it = definitions_.find(n);
    if (def_it == definitions_.end())
        return state;
    const auto elapsed = (now - state.last_updated_at).ticks;
    if (elapsed > 0 && def_it->second.decay_per_tick != 0)
    {
        const auto raw = state.value - (elapsed * def_it->second.decay_per_tick);
        state.value = Clamp(def_it->second, raw);
        state.threshold = ThresholdFor(def_it->second, state.value);
    }
    return state;
}
foundation::Result<void> NeedsLifeService::CommitNeedEvaluation(GameplayObjectRef s, NeedTypeId n,
                                                                GameplayTimePoint now, GameplayContext c)
{
    auto key = StateKey(s, n);
    auto it = states_.find(key);
    if (it == states_.end() || !definitions_.contains(n))
        return foundation::Result<void>::Failure(Error("gameplay.needs.state_missing", "need state missing"));
    auto evaluated = EvaluateNeed(s, n, now);
    Bump();
    auto previous = it->second.threshold;
    evaluated.last_updated_at = now;
    evaluated.revision = revision_;
    it->second = evaluated;
    Record({0, NeedsLifeChangeKind::NeedChanged, s, n, {}, evaluated.threshold, c, revision_});
    if (previous != evaluated.threshold)
    {
        ++diagnostics_.threshold_events;
        Record({0, NeedsLifeChangeKind::NeedThresholdCrossed, s, n, {}, evaluated.threshold, c, revision_});
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> NeedsLifeService::SatisfyNeed(SatisfyNeedRequest r)
{
    if (!r.subject.IsValid() || !definitions_.contains(r.need))
        return foundation::Result<void>::Failure(Error("gameplay.needs.invalid_satisfy", "invalid satisfy request"));
    auto key = StateKey(r.subject, r.need);
    auto it = states_.find(key);
    if (it == states_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.state_missing", "need state missing"));
    const auto &def = definitions_.at(r.need);
    auto state = EvaluateNeed(r.subject, r.need, r.context.time);
    const auto previous = state.threshold;
    state.value = Clamp(def, state.value + r.amount);
    state.threshold = ThresholdFor(def, state.value);
    state.last_updated_at = r.context.time;
    Bump();
    state.revision = revision_;
    it->second = state;
    Record({0, NeedsLifeChangeKind::NeedSatisfied, r.subject, r.need, {}, state.threshold, r.context, revision_});
    if (previous != state.threshold)
        Record({0,
                NeedsLifeChangeKind::NeedThresholdCrossed,
                r.subject,
                r.need,
                {},
                state.threshold,
                r.context,
                revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<LifePressureId> NeedsLifeService::CreateLifePressure(LifePressure p, GameplayContext c)
{
    if (!p.subject.IsValid() || !p.type.IsValid())
        return foundation::Result<LifePressureId>::Failure(
            Error("gameplay.needs.invalid_pressure", "invalid pressure"));
    if (!p.id.IsValid())
        p.id = LifePressureId{pressure_ids_.Next()};
    if (pressures_.contains(p.id))
        return foundation::Result<LifePressureId>::Failure(
            Error("gameplay.needs.duplicate_pressure", "duplicate pressure"));
    Bump();
    p.revision = revision_;
    auto id = p.id;
    auto subject = p.subject;
    pressures_.emplace(id, p);
    Record({0, NeedsLifeChangeKind::LifePressureCreated, subject, {}, id, NeedThreshold::Satisfied, c, revision_});
    return foundation::Result<LifePressureId>::Success(id);
}
foundation::Result<void> NeedsLifeService::ResolveLifePressure(LifePressureId id, GameplayContext c)
{
    auto it = pressures_.find(id);
    if (it == pressures_.end())
        return foundation::Result<void>::Failure(Error("gameplay.needs.pressure_missing", "pressure missing"));
    Bump();
    it->second.state = LifePressureState::Resolved;
    it->second.revision = revision_;
    Record({0,
            NeedsLifeChangeKind::LifePressureResolved,
            it->second.subject,
            {},
            id,
            NeedThreshold::Satisfied,
            c,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<LifeRoutineId> NeedsLifeService::SetRoutine(LifeRoutine r, GameplayContext c)
{
    if (!r.subject.IsValid())
        return foundation::Result<LifeRoutineId>::Failure(Error("gameplay.needs.invalid_routine", "invalid routine"));
    if (!r.id.IsValid())
        r.id = LifeRoutineId{routine_ids_.Next()};
    Bump();
    r.revision = revision_;
    auto id = r.id;
    auto subject = r.subject;
    routines_[id] = r;
    Record({0, NeedsLifeChangeKind::RoutineChanged, subject, {}, {}, NeedThreshold::Satisfied, c, revision_});
    return foundation::Result<LifeRoutineId>::Success(id);
}
foundation::Result<void> NeedsLifeService::SimulateLifeInterval(LifeSimulationRequest r)
{
    if (!r.subject.IsValid() || r.to < r.from)
        return foundation::Result<void>::Failure(
            Error("gameplay.needs.invalid_simulation", "invalid simulation request"));
    std::vector<NeedTypeId> needs;
    for (const auto &[id, p] : profiles_)
    {
        (void)id;
        if (p.subject == r.subject)
        {
            needs = p.active_needs;
            break;
        }
    }
    for (auto n : needs)
    {
        auto committed = CommitNeedEvaluation(r.subject, n, r.to, r.context);
        if (!committed)
            return committed;
    }
    ++diagnostics_.abstract_simulation_steps;
    Record({0,
            NeedsLifeChangeKind::LifeSimulationStepCompleted,
            r.subject,
            {},
            {},
            NeedThreshold::Satisfied,
            r.context,
            revision_});
    return foundation::Result<void>::Success();
}
const NeedProfile *NeedsLifeService::GetNeedProfile(GameplayObjectRef s) const noexcept
{
    const NeedProfile *best = nullptr;
    for (const auto &[id, p] : profiles_)
    {
        (void)id;
        if (p.subject == s && (!best || p.id < best->id))
            best = &p;
    }
    return best;
}
const NeedState *NeedsLifeService::GetCommittedNeedState(GameplayObjectRef s, NeedTypeId n) const noexcept
{
    auto it = states_.find(StateKey(s, n));
    return it == states_.end() ? nullptr : &it->second;
}
std::int64_t NeedsLifeService::GetNeedUrgency(GameplayObjectRef s, NeedTypeId n, GameplayTimePoint now) const
{
    auto state = EvaluateNeed(s, n, now);
    auto it = definitions_.find(n);
    if (it == definitions_.end())
        return 0;
    return it->second.max_value - state.value;
}
std::vector<NeedState> NeedsLifeService::FindCriticalNeeds(GameplayTimePoint now) const
{
    std::vector<NeedState> out;
    for (const auto &[key, st] : states_)
    {
        (void)key;
        auto e = EvaluateNeed(st.subject, st.type, now);
        if (e.threshold == NeedThreshold::Critical)
            out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.subject != b.subject)
            return a.subject < b.subject;
        return a.type < b.type;
    });
    return out;
}
std::vector<LifePressure> NeedsLifeService::FindLifePressures(GameplayObjectRef s) const
{
    std::vector<LifePressure> out;
    for (const auto &[id, p] : pressures_)
    {
        (void)id;
        if (p.subject == s && p.state == LifePressureState::Active)
            out.push_back(p);
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
    const LifeRoutine *best = nullptr;
    for (const auto &[id, r] : routines_)
    {
        (void)id;
        if (r.subject == s && (!best || r.id < best->id))
            best = &r;
    }
    return best;
}
std::vector<NeedsLifeChange> NeedsLifeService::ChangesSince(std::uint64_t seq) const
{
    std::vector<NeedsLifeChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](const auto &c) { return c.sequence > seq; });
    return out;
}
NeedsLifeSnapshot NeedsLifeService::CaptureSnapshot() const
{
    NeedsLifeSnapshot s;
    for (const auto &[id, d] : definitions_)
    {
        (void)id;
        s.definitions.push_back(d);
    }
    for (const auto &[id, p] : profiles_)
    {
        (void)id;
        s.profiles.push_back(p);
    }
    for (const auto &[key, st] : states_)
    {
        (void)key;
        s.states.push_back(st);
    }
    for (const auto &[id, p] : pressures_)
    {
        (void)id;
        s.pressures.push_back(p);
    }
    for (const auto &[id, r] : routines_)
    {
        (void)id;
        s.routines.push_back(r);
    }
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
    return s;
}
foundation::Result<void> NeedsLifeService::RestoreSnapshot(NeedsLifeSnapshot s)
{
    definitions_.clear();
    profiles_.clear();
    states_.clear();
    pressures_.clear();
    routines_.clear();
    for (auto &d : s.definitions)
    {
        if (!d.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid definition"));
        definitions_[d.id] = d;
    }
    for (auto &p : s.profiles)
    {
        if (!p.id.IsValid() || !p.subject.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid profile"));
        profiles_[p.id] = p;
    }
    for (auto &st : s.states)
    {
        if (!st.subject.IsValid() || !st.type.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid state"));
        states_[StateKey(st.subject, st.type)] = st;
    }
    for (auto &p : s.pressures)
    {
        if (!p.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid pressure"));
        pressures_[p.id] = p;
    }
    for (auto &r : s.routines)
    {
        if (!r.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.needs.restore_invalid", "invalid routine"));
        routines_[r.id] = r;
    }
    profile_ids_.Restore(s.profile_ids);
    pressure_ids_.Restore(s.pressure_ids);
    routine_ids_.Restore(s.routine_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
NeedsLifeDiagnostics NeedsLifeService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.definitions = definitions_.size();
    d.profiles = profiles_.size();
    d.states = states_.size();
    d.routines = routines_.size();
    for (const auto &[id, p] : pressures_)
    {
        (void)id;
        if (p.state == LifePressureState::Active)
            ++d.life_pressures;
    }
    for (const auto &[key, st] : states_)
    {
        (void)key;
        if (st.threshold == NeedThreshold::Critical)
            ++d.critical_needs;
    }
    return d;
}
NeedThreshold NeedsLifeService::ThresholdFor(const NeedDefinition &d, std::int64_t value) const noexcept
{
    const auto span = std::max<std::int64_t>(1, d.max_value - d.min_value);
    const auto micro = ((value - d.min_value) * 1'000'000) / span;
    if (micro >= 750'000)
        return NeedThreshold::Satisfied;
    if (micro >= 500'000)
        return NeedThreshold::Low;
    if (micro >= 250'000)
        return NeedThreshold::Medium;
    if (micro > 0)
        return NeedThreshold::High;
    return NeedThreshold::Critical;
}
NeedStateKey NeedsLifeService::StateKey(GameplayObjectRef s, NeedTypeId n) noexcept
{
    return {s, n};
}
void NeedsLifeService::Record(NeedsLifeChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::needs_life
