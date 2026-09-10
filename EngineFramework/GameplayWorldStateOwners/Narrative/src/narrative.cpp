#include "Epidemic/GameFramework/Narrative/narrative.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <type_traits>
#include <tuple>
#include <exception>

namespace epidemic::gameplay::narrative
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}
bool SameEventPayload(const NarrativeEvent &a, const NarrativeEvent &b)
{
    return a.tags.Values() == b.tags.Values() && a.payload == b.payload;
}
template <class TWrappedId>
void AdvanceGeneratorPast(MonotonicIdGenerator<GameplayObjectId> &generator, TWrappedId id) noexcept
{
    if (!id.IsValid())
        return;
    auto snapshot = generator.GetSnapshot();
    if (id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next)
        return;
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}


template <class TEnum, TEnum Last>
[[nodiscard]] constexpr bool IsEnumValueValid(TEnum value) noexcept
{
    using U = std::underlying_type_t<TEnum>;
    const auto raw = static_cast<U>(value);
    return raw >= static_cast<U>(0) && raw <= static_cast<U>(Last);
}

[[nodiscard]] constexpr bool IsValid(NarrativeRuntimeState value) noexcept
{
    return IsEnumValueValid<NarrativeRuntimeState, NarrativeRuntimeState::Locked>(value);
}
[[nodiscard]] constexpr bool IsValid(NarrativeObjectiveRuntimeState value) noexcept
{
    return IsEnumValueValid<NarrativeObjectiveRuntimeState, NarrativeObjectiveRuntimeState::OptionalMissed>(value);
}
[[nodiscard]] constexpr bool IsValid(ConditionEvaluationState value) noexcept
{
    return IsEnumValueValid<ConditionEvaluationState, ConditionEvaluationState::Partial>(value);
}
[[nodiscard]] constexpr bool IsValid(ConsequenceExecutionState value) noexcept
{
    return IsEnumValueValid<ConsequenceExecutionState, ConsequenceExecutionState::AlreadyApplied>(value);
}
[[nodiscard]] constexpr bool IsValid(JournalVisibilityState value) noexcept
{
    return IsEnumValueValid<JournalVisibilityState, JournalVisibilityState::Suppressed>(value);
}
[[nodiscard]] constexpr bool IsValid(RumorState value) noexcept
{
    return IsEnumValueValid<RumorState, RumorState::Suppressed>(value);
}
[[nodiscard]] constexpr bool IsValid(NarrativeChoiceState value) noexcept
{
    return IsEnumValueValid<NarrativeChoiceState, NarrativeChoiceState::Expired>(value);
}
[[nodiscard]] constexpr bool IsValid(NarrativeEventExecutionState value) noexcept
{
    return IsEnumValueValid<NarrativeEventExecutionState, NarrativeEventExecutionState::FailedRetryable>(value);
}
[[nodiscard]] constexpr bool IsValid(NarrativeEventExecutionPhase value) noexcept
{
    return IsEnumValueValid<NarrativeEventExecutionPhase, NarrativeEventExecutionPhase::Completed>(value);
}

template <class Map, class Key, class Value>
[[nodiscard]] bool StageInsert(Map &target, Key key, Value value) noexcept
{
    try
    {
        Map staged = target;
        staged.emplace(std::move(key), std::move(value));
        target.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace

foundation::Result<Revision> NarrativeService::PrepareRevision() const noexcept
{
    const auto next = CheckedNext(revision_);
    if (!next)
        return foundation::Result<Revision>::Failure(
            Error("gameplay.narrative.revision_exhausted", "narrative revision counter is exhausted"));
    return foundation::Result<Revision>::Success(*next);
}

bool NarrativeService::CanAdvanceRevisionBy(std::size_t count) const noexcept
{
    return count <= std::numeric_limits<std::uint64_t>::max() - revision_.value;
}

foundation::Result<void> NarrativeService::RegisterThreadDefinition(NarrativeThreadDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.frozen", "narrative definitions are frozen"));
    if (!d.id.IsValid() || !IsValid(d.initial_state))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_thread", "invalid thread definition"));
    if (thread_defs_.contains(d.id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.duplicate_thread", "duplicate thread definition"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    d.revision = next_revision.Value();
    if (!StageInsert(thread_defs_, d.id, d))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.allocation_failed", "failed to stage thread definition"));
    CommitRevision(next_revision.Value());
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::RegisterArcDefinition(NarrativeArcDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.frozen", "narrative definitions are frozen"));
    if (!d.id.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_arc", "invalid arc definition"));
    if (arc_defs_.contains(d.id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.duplicate_arc", "duplicate arc definition"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    d.revision = next_revision.Value();
    if (!StageInsert(arc_defs_, d.id, d))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.allocation_failed", "failed to stage arc definition"));
    CommitRevision(next_revision.Value());
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::RegisterBeatDefinition(NarrativeBeatDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.frozen", "narrative definitions are frozen"));
    if (!d.id.IsValid() || !d.thread.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_beat", "invalid beat definition"));
    if (beat_defs_.contains(d.id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.duplicate_beat", "duplicate beat definition"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    d.revision = next_revision.Value();
    if (!StageInsert(beat_defs_, d.id, d))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.allocation_failed", "failed to stage beat definition"));
    CommitRevision(next_revision.Value());
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::RegisterObjectiveDefinition(NarrativeObjectiveDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.frozen", "narrative definitions are frozen"));
    if (!d.id.IsValid() || !d.thread.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_objective", "invalid objective definition"));
    if (objective_defs_.contains(d.id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.duplicate_objective", "duplicate objective definition"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    d.revision = next_revision.Value();
    if (!StageInsert(objective_defs_, d.id, d))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.allocation_failed", "failed to stage objective definition"));
    CommitRevision(next_revision.Value());
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::RegisterConditionDefinition(NarrativeConditionDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.frozen", "narrative definitions are frozen"));
    if (!d.id.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_condition", "invalid condition definition"));
    if (condition_defs_.contains(d.id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.duplicate_condition", "duplicate condition definition"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    d.revision = next_revision.Value();
    if (!StageInsert(condition_defs_, d.id, d))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.allocation_failed", "failed to stage condition definition"));
    CommitRevision(next_revision.Value());
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::RegisterConsequenceDefinition(NarrativeConsequenceDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.frozen", "narrative definitions are frozen"));
    if (!d.id.IsValid() || !d.type.IsValid() || (d.journal_template && !IsValid(d.journal_template->visibility)))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_consequence", "invalid consequence definition"));
    if (consequence_defs_.contains(d.id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.duplicate_consequence", "duplicate consequence definition"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    d.revision = next_revision.Value();
    if (!StageInsert(consequence_defs_, d.id, d))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.allocation_failed", "failed to stage consequence definition"));
    CommitRevision(next_revision.Value());
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::RegisterStoryletDefinition(StoryletDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.frozen", "narrative definitions are frozen"));
    if (!d.id.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_storylet", "invalid storylet definition"));
    if (storylet_defs_.contains(d.id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.duplicate_storylet", "duplicate storylet definition"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    d.revision = next_revision.Value();
    try
    {
        auto staged_defs = storylet_defs_;
        auto staged_runtime = storylet_runtime_;
        staged_defs.emplace(d.id, d);
        staged_runtime.emplace(d.id, StoryletRuntimeState{.id = d.id, .revision = next_revision.Value()});
        storylet_defs_.swap(staged_defs);
        storylet_runtime_.swap(staged_runtime);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.narrative.allocation_failed", "failed to stage storylet definition"));
    }
    CommitRevision(next_revision.Value());
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::RegisterConditionResolver(NarrativeConditionTypeId type,
                                                                     const INarrativeConditionResolver &resolver)
{
    if (!type.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.invalid_resolver", "invalid condition resolver"));
    if (condition_resolvers_.contains(type))
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.duplicate_resolver", "duplicate condition resolver"));
    try
    {
        condition_resolvers_.emplace(type, &resolver);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to register condition resolver"));
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::RegisterConsequenceHandler(NarrativeConsequenceTypeId type,
                                                                      const INarrativeConsequenceHandler &handler)
{
    if (!type.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.invalid_handler", "invalid consequence handler"));
    if (consequence_handlers_.contains(type))
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.duplicate_handler", "duplicate consequence handler"));
    try
    {
        consequence_handlers_.emplace(type, &handler);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to register consequence handler"));
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::FreezeDefinitions()
{
    for (const auto &[id, consequence] : consequence_defs_)
    {
        (void)id;
        if (IsBuiltinAddJournal(consequence.type))
        {
            if (!consequence.journal_template.has_value() || !consequence.journal_template->type.IsValid())
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.invalid_builtin_consequence",
                          "narrative.add_journal requires a valid journal template"));
        }
        if (IsBuiltinCreateRumor(consequence.type))
        {
            if (!consequence.rumor_template.has_value() || !consequence.rumor_template->topic.IsValid() ||
                consequence.rumor_template->confidence < 0 || consequence.rumor_template->confidence > 1'000'000 ||
                consequence.rumor_template->lifetime.ticks < 0)
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.invalid_builtin_consequence",
                          "narrative.create_rumor requires a valid data-driven rumor template"));
        }
    }
    for (const auto &[id, condition] : condition_defs_)
    {
        (void)id;
        if (!condition.all_of.empty() && !condition.any_of.empty())
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.ambiguous_condition",
                      "condition cannot define both all_of and any_of composition"));
        for (auto nested : condition.all_of)
        {
            if (!condition_defs_.contains(nested))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_condition", "condition references unknown all_of condition"));
        }
        for (auto nested : condition.any_of)
        {
            if (!condition_defs_.contains(nested))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_condition", "condition references unknown any_of condition"));
        }
    }
    for (const auto &[id, thread] : thread_defs_)
    {
        (void)id;
        for (auto beat : thread.beats)
        {
            if (!beat_defs_.contains(beat))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_beat", "thread references unknown beat"));
            if (beat_defs_.at(beat).thread != thread.id)
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.thread_mismatch", "thread references beat owned by another thread"));
        }
        for (auto objective : thread.root_objectives)
        {
            if (!objective_defs_.contains(objective))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_objective", "thread references unknown objective"));
            if (objective_defs_.at(objective).thread != thread.id)
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.thread_mismatch",
                          "thread references root objective owned by another thread"));
        }
    }
    for (const auto &[id, arc] : arc_defs_)
    {
        (void)id;
        for (auto thread : arc.threads)
            if (!thread_defs_.contains(thread))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_thread", "arc references unknown thread"));
    }
    for (const auto &[id, beat] : beat_defs_)
    {
        (void)id;
        if (!thread_defs_.contains(beat.thread))
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.unknown_thread", "beat references unknown thread"));
        for (auto c : beat.activation_conditions)
        {
            if (!condition_defs_.contains(c))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_condition", "beat references unknown activation condition"));
        }
        for (auto c : beat.completion_conditions)
        {
            if (!condition_defs_.contains(c))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_condition", "beat references unknown completion condition"));
        }
        for (auto c : beat.consequences)
        {
            if (!consequence_defs_.contains(c))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_consequence", "beat references unknown consequence"));
        }
    }
    for (const auto &[id, obj] : objective_defs_)
    {
        (void)id;
        if (!thread_defs_.contains(obj.thread))
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.unknown_thread", "objective references unknown thread"));
        for (auto c : obj.start_conditions)
        {
            if (!condition_defs_.contains(c))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_condition", "objective references unknown start condition"));
        }
        for (auto c : obj.completion_conditions)
        {
            if (!condition_defs_.contains(c))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_condition", "objective references unknown completion condition"));
        }
        for (auto c : obj.failure_conditions)
        {
            if (!condition_defs_.contains(c))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_condition", "objective references unknown failure condition"));
        }
    }
    for (const auto &[id, s] : storylet_defs_)
    {
        (void)id;
        for (auto c : s.availability_conditions)
        {
            if (!condition_defs_.contains(c))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_condition", "storylet references unknown condition"));
        }
        for (auto c : s.consequences)
        {
            if (!consequence_defs_.contains(c))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.unknown_consequence", "storylet references unknown consequence"));
        }
    }
    if (!ValidateConditionGraphAcyclic())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.condition_cycle", "narrative condition graph contains a cycle"));
    frozen_ = true;
    return foundation::Result<void>::Success();
}

bool NarrativeService::ValidateConditionGraphAcyclic() const
{
    std::unordered_map<NarrativeConditionId, std::uint8_t, IdHash> marks;
    for (const auto &[id, definition] : condition_defs_)
    {
        (void)definition;
        if (HasConditionCycle(id, marks))
            return false;
    }
    return true;
}

bool NarrativeService::HasConditionCycle(
    NarrativeConditionId id, std::unordered_map<NarrativeConditionId, std::uint8_t, IdHash> &marks) const
{
    const auto mark = marks[id];
    if (mark == 1)
        return true;
    if (mark == 2)
        return false;
    marks[id] = 1;
    const auto it = condition_defs_.find(id);
    if (it != condition_defs_.end())
    {
        for (auto nested : it->second.all_of)
            if (HasConditionCycle(nested, marks))
                return true;
        for (auto nested : it->second.any_of)
            if (HasConditionCycle(nested, marks))
                return true;
    }
    marks[id] = 2;
    return false;
}

NarrativeThreadState &NarrativeService::EnsureThreadState(NarrativeThreadId id)
{
    auto it = threads_.find(id);
    if (it != threads_.end())
        return it->second;
    NarrativeThreadState s;
    s.thread = id;
    if (auto def = thread_defs_.find(id); def != thread_defs_.end())
        s.state = def->second.initial_state;
    s.revision = revision_;
    return threads_.emplace(id, s).first->second;
}
NarrativeObjectiveState &NarrativeService::EnsureObjectiveState(NarrativeObjectiveId id)
{
    auto it = objectives_.find(id);
    if (it != objectives_.end())
        return it->second;
    NarrativeObjectiveState s;
    s.objective = id;
    s.revision = revision_;
    return objectives_.emplace(id, s).first->second;
}

foundation::Result<void> NarrativeService::StartThread(NarrativeThreadId id, GameplayObjectRef owner,
                                                       GameplayObjectRef scope, GameplayContext c)
{
    const auto def_it = thread_defs_.find(id);
    if (def_it == thread_defs_.end())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_missing", "thread missing"));

    auto existing = threads_.find(id);
    if (existing != threads_.end())
    {
        if (existing->second.state == NarrativeRuntimeState::Completed ||
            existing->second.state == NarrativeRuntimeState::Failed)
            return foundation::Result<void>::Success();
        if (existing->second.state == NarrativeRuntimeState::Expired)
            return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_expired", "thread is expired"));
        if (existing->second.state == NarrativeRuntimeState::Active)
            return foundation::Result<void>::Success();
        if (existing->second.state == NarrativeRuntimeState::Suspended)
            return ResumeThread(id, c);
    }

    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());

    if (existing == threads_.end())
    {
        NarrativeThreadState staged;
        staged.thread = id;
        staged.state = NarrativeRuntimeState::Active;
        staged.owner_subject = owner;
        staged.scope = scope;
        staged.started_at = c.time;
        staged.updated_at = c.time;
        staged.revision = next_revision.Value();
        try
        {
            existing = threads_.emplace(id, std::move(staged)).first;
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.allocation_failed", "failed to create thread runtime state"));
        }
    }
    else
    {
        existing->second.state = NarrativeRuntimeState::Active;
        existing->second.owner_subject = owner;
        existing->second.scope = scope;
        existing->second.started_at = c.time;
        existing->second.updated_at = c.time;
        existing->second.revision = next_revision.Value();
    }
    CommitRevision(next_revision.Value());
    Record({0, NarrativeChangeKind::ThreadStarted, id, {}, {}, {}, {}, {}, {}, {}, owner, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::SuspendThread(NarrativeThreadId id, GameplayContext c)
{
    auto it = threads_.find(id);
    if (it == threads_.end())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_missing", "thread missing"));
    if (it->second.state != NarrativeRuntimeState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.thread_transition_invalid", "only an active thread can be suspended"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    it->second.state = NarrativeRuntimeState::Suspended;
    it->second.updated_at = c.time;
    it->second.revision = revision_;
    Record({0,
            NarrativeChangeKind::ThreadSuspended,
            id,
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            it->second.owner_subject,
            c,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::ResumeThread(NarrativeThreadId id, GameplayContext c)
{
    auto it = threads_.find(id);
    if (it == threads_.end() || !thread_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_missing", "thread missing"));
    if (it->second.state == NarrativeRuntimeState::Active)
        return foundation::Result<void>::Success();
    if (it->second.state != NarrativeRuntimeState::Suspended)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.thread_transition_invalid", "only a suspended thread can be resumed"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    it->second.state = NarrativeRuntimeState::Active;
    it->second.updated_at = c.time;
    it->second.revision = revision_;
    Record({0, NarrativeChangeKind::ThreadResumed, id, {}, {}, {}, {}, {}, {}, {}, it->second.owner_subject, c,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::ExpireThread(NarrativeThreadId id, GameplayContext c)
{
    auto it = threads_.find(id);
    if (it == threads_.end() || !thread_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_missing", "thread missing"));
    if (it->second.state == NarrativeRuntimeState::Completed || it->second.state == NarrativeRuntimeState::Failed ||
        it->second.state == NarrativeRuntimeState::Expired)
        return foundation::Result<void>::Success();
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    it->second.state = NarrativeRuntimeState::Expired;
    it->second.updated_at = c.time;
    it->second.revision = revision_;
    Record({0, NarrativeChangeKind::ThreadExpired, id, {}, {}, {}, {}, {}, {}, {}, it->second.owner_subject, c,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::CompleteThread(NarrativeThreadId id, GameplayContext c)
{
    if (!thread_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_missing", "thread missing"));
    auto it = threads_.find(id);
    if (it == threads_.end())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_not_started", "thread not started"));
    auto &s = it->second;
    if (s.state == NarrativeRuntimeState::Completed)
        return foundation::Result<void>::Success();
    if (s.state == NarrativeRuntimeState::Expired)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_expired", "thread is expired"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    s.state = NarrativeRuntimeState::Completed;
    s.updated_at = c.time;
    s.revision = revision_;
    Record({0, NarrativeChangeKind::ThreadCompleted, id, {}, {}, {}, {}, {}, {}, {}, s.owner_subject, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::FailThread(NarrativeThreadId id, GameplayContext c)
{
    if (!thread_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_missing", "thread missing"));
    auto it = threads_.find(id);
    if (it == threads_.end())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.thread_not_started", "thread not started"));
    auto &s = it->second;
    if (s.state == NarrativeRuntimeState::Failed)
        return foundation::Result<void>::Success();
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    s.state = NarrativeRuntimeState::Failed;
    s.updated_at = c.time;
    s.revision = revision_;
    Record({0, NarrativeChangeKind::ThreadFailed, id, {}, {}, {}, {}, {}, {}, {}, s.owner_subject, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::ActivateObjective(NarrativeObjectiveId id, GameplayContext c)
{
    const auto def_it = objective_defs_.find(id);
    if (def_it == objective_defs_.end())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.objective_missing", "objective missing"));
    auto existing = objectives_.find(id);
    if (existing != objectives_.end() &&
        (existing->second.state == NarrativeObjectiveRuntimeState::Completed ||
         existing->second.state == NarrativeObjectiveRuntimeState::Failed))
        return foundation::Result<void>::Success();

    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());

    if (existing == objectives_.end())
    {
        NarrativeObjectiveState staged;
        staged.objective = id;
        staged.state = NarrativeObjectiveRuntimeState::Active;
        staged.started_at = c.time;
        staged.revision = next_revision.Value();
        try
        {
            existing = objectives_.emplace(id, std::move(staged)).first;
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.allocation_failed", "failed to create objective runtime state"));
        }
    }
    else
    {
        existing->second.state = NarrativeObjectiveRuntimeState::Active;
        existing->second.started_at = c.time;
        existing->second.revision = next_revision.Value();
    }
    CommitRevision(next_revision.Value());
    Record({0, NarrativeChangeKind::ObjectiveActivated, def_it->second.thread, id, {}, {}, {}, {}, {}, {}, c.actor, c,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::CompleteObjective(NarrativeObjectiveId id, GameplayContext c)
{
    if (!objective_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.objective_missing", "objective missing"));
    auto it = objectives_.find(id);
    if (it == objectives_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.objective_not_active", "objective has no runtime state"));
    auto &s = it->second;
    if (s.state == NarrativeObjectiveRuntimeState::Completed)
        return foundation::Result<void>::Success();
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    s.state = NarrativeObjectiveRuntimeState::Completed;
    s.progress = 1'000'000;
    s.completed_at = c.time;
    s.revision = revision_;
    auto thread = objective_defs_.at(id).thread;
    Record({0, NarrativeChangeKind::ObjectiveCompleted, thread, id, {}, {}, {}, {}, {}, {}, c.actor, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::FailObjective(NarrativeObjectiveId id, GameplayContext c)
{
    if (!objective_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.objective_missing", "objective missing"));
    auto it = objectives_.find(id);
    if (it == objectives_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.objective_not_active", "objective has no runtime state"));
    auto &s = it->second;
    if (s.state == NarrativeObjectiveRuntimeState::Failed)
        return foundation::Result<void>::Success();
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    s.state = NarrativeObjectiveRuntimeState::Failed;
    s.completed_at = c.time;
    s.revision = revision_;
    auto thread = objective_defs_.at(id).thread;
    Record({0, NarrativeChangeKind::ObjectiveFailed, thread, id, {}, {}, {}, {}, {}, {}, c.actor, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::CancelObjective(NarrativeObjectiveId id, GameplayContext c)
{
    if (!objective_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.objective_missing", "objective missing"));
    auto it = objectives_.find(id);
    if (it == objectives_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.objective_not_active", "objective has no runtime state"));
    if (it->second.state == NarrativeObjectiveRuntimeState::Cancelled)
        return foundation::Result<void>::Success();
    if (it->second.state == NarrativeObjectiveRuntimeState::Completed ||
        it->second.state == NarrativeObjectiveRuntimeState::Failed ||
        it->second.state == NarrativeObjectiveRuntimeState::OptionalMissed)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.objective_transition_invalid", "objective is terminal"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    it->second.state = NarrativeObjectiveRuntimeState::Cancelled;
    it->second.completed_at = c.time;
    it->second.revision = revision_;
    Record({0, NarrativeChangeKind::ObjectiveCancelled, objective_defs_.at(id).thread, id, {}, {}, {}, {}, {}, {},
            c.actor, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::MarkObjectiveOptionalMissed(NarrativeObjectiveId id, GameplayContext c)
{
    if (!objective_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.objective_missing", "objective missing"));
    auto it = objectives_.find(id);
    if (it == objectives_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.objective_not_active", "objective has no runtime state"));
    if (it->second.state == NarrativeObjectiveRuntimeState::OptionalMissed)
        return foundation::Result<void>::Success();
    if (it->second.state == NarrativeObjectiveRuntimeState::Completed ||
        it->second.state == NarrativeObjectiveRuntimeState::Failed ||
        it->second.state == NarrativeObjectiveRuntimeState::Cancelled)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.objective_transition_invalid", "objective is terminal"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    it->second.state = NarrativeObjectiveRuntimeState::OptionalMissed;
    it->second.completed_at = c.time;
    it->second.revision = revision_;
    Record({0, NarrativeChangeKind::ObjectiveOptionalMissed, objective_defs_.at(id).thread, id, {}, {}, {}, {}, {},
            {}, c.actor, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::UpdateObjectiveProgress(NarrativeObjectiveId id, std::int64_t progress,
                                                                   GameplayContext c)
{
    if (!objective_defs_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.narrative.objective_missing", "objective missing"));
    auto it = objectives_.find(id);
    if (it == objectives_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.objective_not_active", "objective has no runtime state"));
    auto &s = it->second;
    if (s.state != NarrativeObjectiveRuntimeState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.objective_transition_invalid", "only an active objective has progress"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    CommitRevision(next_revision.Value());
    s.progress = std::clamp(progress, std::int64_t{0}, std::int64_t{1'000'000});
    s.revision = revision_;
    auto thread = objective_defs_.at(id).thread;
    Record(
        {0, NarrativeChangeKind::ObjectiveProgressChanged, thread, id, {}, {}, {}, {}, {}, {}, c.actor, c, revision_});
    return foundation::Result<void>::Success();
}

NarrativeConditionResult NarrativeService::EvaluateCondition(NarrativeConditionId id,
                                                             NarrativeEvaluationContext ctx) const
{
    NarrativeConditionResult r;
    r.condition = id;
    if (evaluation_counter_ >= evaluation_limit_)
    {
        evaluation_budget_exhausted_ = true;
        r.state = ConditionEvaluationState::Unavailable;
        return r;
    }
    if (evaluation_depth_ >= kMaxConditionDepth)
    {
        r.state = ConditionEvaluationState::Unavailable;
        return r;
    }
    ++diagnostics_.condition_evaluations;
    ++evaluation_counter_;
    ++evaluation_depth_;
    struct DepthGuard
    {
        std::size_t &depth;
        ~DepthGuard() { --depth; }
    } guard{evaluation_depth_};
    auto it = condition_defs_.find(id);
    if (it == condition_defs_.end())
        return r;
    if (!it->second.all_of.empty())
    {
        r.state = AllSatisfied(it->second.all_of, ctx) ? ConditionEvaluationState::Satisfied
                                                       : ConditionEvaluationState::Unsatisfied;
        return r;
    }
    if (!it->second.any_of.empty())
    {
        r.state = AnySatisfied(it->second.any_of, ctx) ? ConditionEvaluationState::Satisfied
                                                       : ConditionEvaluationState::Unsatisfied;
        return r;
    }
    if (!it->second.type.IsValid())
    {
        r.state = ConditionEvaluationState::Satisfied;
        return r;
    }
    auto resolver = condition_resolvers_.find(it->second.type);
    if (resolver == condition_resolvers_.end())
    {
        r.state = ConditionEvaluationState::Unavailable;
        return r;
    }
    try
    {
        auto result = resolver->second->Evaluate(it->second, ctx);
        if (!IsValid(result.state))
        {
            result.condition = id;
            result.state = ConditionEvaluationState::Unavailable;
        }
        return result;
    }
    catch (...)
    {
        r.state = ConditionEvaluationState::Unavailable;
        return r;
    }
}
bool NarrativeService::AllSatisfied(const std::vector<NarrativeConditionId> &conditions,
                                    const NarrativeEvaluationContext &context) const
{
    for (auto id : conditions)
    {
        if (EvaluateCondition(id, context).state != ConditionEvaluationState::Satisfied)
            return false;
    }
    return true;
}
bool NarrativeService::AnySatisfied(const std::vector<NarrativeConditionId> &conditions,
                                    const NarrativeEvaluationContext &context) const
{
    for (auto id : conditions)
    {
        if (EvaluateCondition(id, context).state == ConditionEvaluationState::Satisfied)
            return true;
    }
    return conditions.empty();
}

foundation::Result<void> NarrativeService::ProcessNarrativeEvent(NarrativeEvent event, NarrativeProcessBudget budget)
{
    const auto event_key = MakeEventKey(event);
    auto existing = event_executions_.find(event_key);
    if (existing != event_executions_.end() && !SameEventPayload(existing->second.event, event))
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.event_key_conflict", "same narrative event key was retried with different payload"));
    if (existing != event_executions_.end() && existing->second.state == NarrativeEventExecutionState::Completed)
    {
        ++diagnostics_.deduplicated_events;
        Record({0,
                NarrativeChangeKind::EventDeduplicated,
                {},
                {},
                {},
                {},
                {},
                {},
                {},
                {},
                event.subject,
                {.time = event.time, .correlation = event.correlation, .actor = event.instigator, .source = event.subject},
                revision_});
        return foundation::Result<void>::Success();
    }

    auto build_work_plan = [&]() -> foundation::Result<std::tuple<std::vector<NarrativeBeatId>,
                                                                    std::vector<NarrativeObjectiveId>,
                                                                    std::vector<StoryletId>>>
    {
        try
        {
            std::vector<NarrativeBeatId> beats;
            std::vector<NarrativeObjectiveId> objectives;
            std::vector<StoryletId> storylets;
            beats.reserve(beat_defs_.size());
            objectives.reserve(objective_defs_.size());
            storylets.reserve(storylet_defs_.size());
            for (const auto &[id, definition] : beat_defs_)
            {
                (void)definition;
                beats.push_back(id);
            }
            std::sort(beats.begin(), beats.end(), [this](auto a, auto b) {
                const auto &left = beat_defs_.at(a);
                const auto &right = beat_defs_.at(b);
                if (left.thread != right.thread)
                    return left.thread < right.thread;
                if (left.order != right.order)
                    return left.order < right.order;
                return left.id < right.id;
            });
            for (const auto &[id, definition] : objective_defs_)
            {
                (void)definition;
                objectives.push_back(id);
            }
            std::sort(objectives.begin(), objectives.end());
            for (const auto &[id, definition] : storylet_defs_)
            {
                (void)definition;
                storylets.push_back(id);
            }
            std::sort(storylets.begin(), storylets.end(), [this](auto a, auto b) {
                const auto &left = storylet_defs_.at(a);
                const auto &right = storylet_defs_.at(b);
                if (left.priority != right.priority)
                    return left.priority > right.priority;
                return left.id < right.id;
            });
            return foundation::Result<std::tuple<std::vector<NarrativeBeatId>, std::vector<NarrativeObjectiveId>,
                                                 std::vector<StoryletId>>>::Success(
                {std::move(beats), std::move(objectives), std::move(storylets)});
        }
        catch (...)
        {
            return foundation::Result<std::tuple<std::vector<NarrativeBeatId>, std::vector<NarrativeObjectiveId>,
                                                 std::vector<StoryletId>>>::Failure(
                Error("gameplay.narrative.allocation_failed", "failed to build narrative event work plan"));
        }
    };

    if (existing == event_executions_.end())
    {
        auto next_revision = PrepareRevision();
        if (!next_revision)
            return foundation::Result<void>::Failure(next_revision.GetError());
        auto plan = build_work_plan();
        if (!plan)
            return foundation::Result<void>::Failure(plan.GetError());
        auto [beats, objectives, storylets] = std::move(plan.Value());
        NarrativeEventExecution execution;
        execution.key = event_key;
        execution.event = event;
        execution.state = NarrativeEventExecutionState::Pending;
        execution.phase = NarrativeEventExecutionPhase::Beats;
        execution.work_plan_initialized = true;
        execution.beat_plan = std::move(beats);
        execution.objective_plan = std::move(objectives);
        execution.storylet_plan = std::move(storylets);
        execution.revision = next_revision.Value();
        try
        {
            existing = event_executions_.emplace(event_key, std::move(execution)).first;
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.allocation_failed", "failed to publish narrative event execution"));
        }
        CommitRevision(next_revision.Value());
    }

    auto &execution = existing->second;
    if (!execution.work_plan_initialized)
    {
        auto plan = build_work_plan();
        if (!plan)
            return foundation::Result<void>::Failure(plan.GetError());
        auto [beats, objectives, storylets] = std::move(plan.Value());
        execution.beat_plan = std::move(beats);
        execution.objective_plan = std::move(objectives);
        execution.storylet_plan = std::move(storylets);
        execution.work_plan_initialized = true;
    }
    execution.state = NarrativeEventExecutionState::Processing;
    execution.event = event;
    execution.revision = revision_;

    NarrativeEvaluationContext ctx{event, event.instigator, event.time};
    evaluation_counter_ = 0;
    evaluation_limit_ = budget.max_condition_evaluations;
    evaluation_depth_ = 0;
    evaluation_budget_exhausted_ = false;
    std::size_t consequences_remaining = budget.max_consequences_planned;
    std::size_t storylets_evaluated = 0;
    std::size_t storylets_activated = 0;

    auto pause_for_budget = [&]() -> foundation::Result<void> {
        ++diagnostics_.budget_exhaustions;
        auto next_revision = PrepareRevision();
        if (!next_revision)
        {
            evaluation_limit_ = static_cast<std::size_t>(-1);
            evaluation_budget_exhausted_ = false;
            return foundation::Result<void>::Failure(next_revision.GetError());
        }
        execution.state = NarrativeEventExecutionState::Pending;
        execution.revision = next_revision.Value();
        CommitRevision(next_revision.Value());
        evaluation_limit_ = static_cast<std::size_t>(-1);
        evaluation_budget_exhausted_ = false;
        return foundation::Result<void>::Success();
    };
    auto fail_retryable = [&](foundation::Result<void> failure) -> foundation::Result<void> {
        auto next_revision = PrepareRevision();
        if (!next_revision)
        {
            evaluation_limit_ = static_cast<std::size_t>(-1);
            evaluation_budget_exhausted_ = false;
            return foundation::Result<void>::Failure(next_revision.GetError());
        }
        execution.state = NarrativeEventExecutionState::FailedRetryable;
        execution.revision = next_revision.Value();
        CommitRevision(next_revision.Value());
        evaluation_limit_ = static_cast<std::size_t>(-1);
        evaluation_budget_exhausted_ = false;
        return failure;
    };
    auto all_satisfied = [&](const std::vector<NarrativeConditionId> &conditions) {
        const bool satisfied = AllSatisfied(conditions, ctx);
        return std::pair<bool, bool>{satisfied, evaluation_budget_exhausted_};
    };

    if (execution.phase == NarrativeEventExecutionPhase::Beats)
    {
        while (execution.next_beat < execution.beat_plan.size())
        {
            const auto &beat = beat_defs_.at(execution.beat_plan[execution.next_beat]);
            if (std::find(activated_beats_.begin(), activated_beats_.end(), beat.id) != activated_beats_.end())
            {
                ++execution.next_beat;
                continue;
            }
            if (!beat.activation_conditions.empty())
            {
                auto [satisfied, exhausted] = all_satisfied(beat.activation_conditions);
                if (exhausted)
                    return pause_for_budget();
                if (!satisfied)
                {
                    ++execution.next_beat;
                    continue;
                }
            }
            if (!thread_defs_.contains(beat.thread))
            {
                ++execution.next_beat;
                continue;
            }
            const auto thread_it = threads_.find(beat.thread);
            if (thread_it == threads_.end() || thread_it->second.state == NarrativeRuntimeState::Hidden ||
                thread_it->second.state == NarrativeRuntimeState::Available ||
                thread_it->second.state == NarrativeRuntimeState::Discovered ||
                thread_it->second.state == NarrativeRuntimeState::Suspended)
            {
                auto started = StartThread(beat.thread, event.instigator, event.area,
                                           {.time = event.time,
                                            .correlation = event.correlation,
                                            .actor = event.instigator,
                                            .source = event.subject});
                if (!started)
                    return fail_retryable(std::move(started));
            }
            if (!beat.completion_conditions.empty())
            {
                auto [satisfied, exhausted] = all_satisfied(beat.completion_conditions);
                if (exhausted)
                    return pause_for_budget();
                if (!satisfied)
                {
                    ++execution.next_beat;
                    continue;
                }
            }

            const auto required = CountUnplannedConsequences(beat.thread, beat.id, {}, beat.consequences,
                                                             event.correlation);
            if (required > consequences_remaining)
                return pause_for_budget();
            std::vector<NarrativeBeatId> staged_activated;
            try
            {
                staged_activated = activated_beats_;
                staged_activated.push_back(beat.id);
            }
            catch (...)
            {
                return fail_retryable(foundation::Result<void>::Failure(
                    Error("gameplay.narrative.allocation_failed", "failed to stage activated beat")));
            }
            auto planned = PlanConsequences(beat.thread, beat.id, {}, beat.consequences, event.correlation, event.time);
            if (!planned)
                return fail_retryable(std::move(planned));
            consequences_remaining -= required;
            activated_beats_.swap(staged_activated);
            ++execution.next_beat;
        }
        execution.phase = NarrativeEventExecutionPhase::Objectives;
    }

    if (execution.phase == NarrativeEventExecutionPhase::Objectives)
    {
        while (execution.next_objective < execution.objective_plan.size())
        {
            const auto &objective = objective_defs_.at(execution.objective_plan[execution.next_objective]);
            auto state_it = objectives_.find(objective.id);
            const auto state_value = state_it == objectives_.end() ? NarrativeObjectiveRuntimeState::Hidden
                                                                   : state_it->second.state;
            if (state_value == NarrativeObjectiveRuntimeState::Hidden ||
                state_value == NarrativeObjectiveRuntimeState::Available)
            {
                auto [satisfied, exhausted] = all_satisfied(objective.start_conditions);
                if (exhausted)
                    return pause_for_budget();
                if (satisfied)
                {
                    auto activated = ActivateObjective(objective.id,
                                                       {.time = event.time,
                                                        .correlation = event.correlation,
                                                        .actor = event.instigator,
                                                        .source = event.subject});
                    if (!activated)
                        return fail_retryable(std::move(activated));
                    state_it = objectives_.find(objective.id);
                }
            }
            const auto current_state = state_it == objectives_.end() ? NarrativeObjectiveRuntimeState::Hidden
                                                                     : state_it->second.state;
            if (current_state == NarrativeObjectiveRuntimeState::Active ||
                current_state == NarrativeObjectiveRuntimeState::Available)
            {
                if (!objective.failure_conditions.empty())
                {
                    auto [failed_condition, exhausted] = all_satisfied(objective.failure_conditions);
                    if (exhausted)
                        return pause_for_budget();
                    if (failed_condition)
                    {
                        auto failed = FailObjective(objective.id,
                                                    {.time = event.time,
                                                     .correlation = event.correlation,
                                                     .actor = event.instigator,
                                                     .source = event.subject});
                        if (!failed)
                            return fail_retryable(std::move(failed));
                        ++execution.next_objective;
                        continue;
                    }
                }
                if (!objective.completion_conditions.empty())
                {
                    auto [completed_condition, exhausted] = all_satisfied(objective.completion_conditions);
                    if (exhausted)
                        return pause_for_budget();
                    if (completed_condition)
                    {
                        auto completed = CompleteObjective(objective.id,
                                                           {.time = event.time,
                                                            .correlation = event.correlation,
                                                            .actor = event.instigator,
                                                            .source = event.subject});
                        if (!completed)
                            return fail_retryable(std::move(completed));
                    }
                }
            }
            ++execution.next_objective;
        }
        execution.phase = NarrativeEventExecutionPhase::Storylets;
    }

    if (execution.phase == NarrativeEventExecutionPhase::Storylets)
    {
        while (execution.next_storylet < execution.storylet_plan.size())
        {
            if (storylets_evaluated >= budget.max_storylets_evaluated)
                return pause_for_budget();
            const auto &storylet = storylet_defs_.at(execution.storylet_plan[execution.next_storylet]);
            ++storylets_evaluated;
            ++diagnostics_.storylets_evaluated;
            auto runtime_it = storylet_runtime_.find(storylet.id);
            if (runtime_it == storylet_runtime_.end())
                return fail_retryable(foundation::Result<void>::Failure(
                    Error("gameplay.narrative.storylet_runtime_missing", "storylet runtime state is missing")));
            auto &runtime = runtime_it->second;
            if (runtime.activations >= storylet.max_activations)
            {
                ++execution.next_storylet;
                continue;
            }
            if (storylet.cooldown.ticks > 0 && runtime.activations > 0 &&
                event.time < SaturatingAdd(runtime.last_activated_at, storylet.cooldown))
            {
                ++execution.next_storylet;
                continue;
            }
            auto [available, exhausted] = all_satisfied(storylet.availability_conditions);
            if (exhausted)
                return pause_for_budget();
            if (!available)
            {
                ++execution.next_storylet;
                continue;
            }
            if (storylets_activated >= budget.max_storylets_activated)
                return pause_for_budget();
            const auto required = CountUnplannedConsequences({}, {}, {}, storylet.consequences, event.correlation);
            if (required > consequences_remaining)
                return pause_for_budget();
            if (!CanAdvanceRevisionBy(required + 1))
                return fail_retryable(foundation::Result<void>::Failure(
                    Error("gameplay.narrative.revision_exhausted", "narrative revision counter is exhausted")));
            auto planned = PlanConsequences({}, {}, {}, storylet.consequences, event.correlation, event.time);
            if (!planned)
                return fail_retryable(std::move(planned));
            consequences_remaining -= required;
            ++storylets_activated;
            ++diagnostics_.storylets_activated;
            auto next_revision = PrepareRevision();
            if (!next_revision)
                return fail_retryable(foundation::Result<void>::Failure(next_revision.GetError()));
            ++runtime.activations;
            runtime.last_activated_at = event.time;
            runtime.revision = next_revision.Value();
            CommitRevision(next_revision.Value());
            Record({0,
                    NarrativeChangeKind::StoryletActivated,
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    event.subject,
                    {.time = event.time,
                     .correlation = event.correlation,
                     .actor = event.instigator,
                     .source = event.subject},
                    revision_});
            ++execution.next_storylet;
        }
        execution.phase = NarrativeEventExecutionPhase::Completed;
    }

    auto completion_revision = PrepareRevision();
    if (!completion_revision)
    {
        evaluation_limit_ = static_cast<std::size_t>(-1);
        evaluation_budget_exhausted_ = false;
        return foundation::Result<void>::Failure(completion_revision.GetError());
    }
    std::unordered_set<NarrativeEventKey, NarrativeEventKeyHash> staged_processed;
    try
    {
        staged_processed = processed_event_keys_;
        staged_processed.insert(event_key);
    }
    catch (...)
    {
        evaluation_limit_ = static_cast<std::size_t>(-1);
        evaluation_budget_exhausted_ = false;
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to stage processed narrative event"));
    }
    execution.state = NarrativeEventExecutionState::Completed;
    execution.phase = NarrativeEventExecutionPhase::Completed;
    execution.revision = completion_revision.Value();
    processed_event_keys_.swap(staged_processed);
    CommitRevision(completion_revision.Value());
    ++diagnostics_.events_processed;
    Record({0,
            NarrativeChangeKind::EventProcessed,
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            event.subject,
            {.time = event.time, .correlation = event.correlation, .actor = event.instigator, .source = event.subject},
            revision_});

    while (processed_event_keys_.size() > kCompletedEventRetention)
    {
        auto victim = event_executions_.end();
        for (auto it = event_executions_.begin(); it != event_executions_.end(); ++it)
        {
            if (it->second.state != NarrativeEventExecutionState::Completed)
                continue;
            if (victim == event_executions_.end() || it->second.event.time < victim->second.event.time ||
                (it->second.event.time == victim->second.event.time && it->first < victim->first))
                victim = it;
        }
        if (victim == event_executions_.end())
            break;
        processed_event_keys_.erase(victim->first);
        event_executions_.erase(victim);
    }
    evaluation_limit_ = static_cast<std::size_t>(-1);
    evaluation_budget_exhausted_ = false;
    return foundation::Result<void>::Success();
}

foundation::Result<void> NarrativeService::PlanConsequences(NarrativeThreadId thread, NarrativeBeatId beat,
                                                            NarrativeObjectiveId objective,
                                                            const std::vector<NarrativeConsequenceId> &consequence_ids,
                                                            CorrelationId correlation, GameplayTimePoint now)
{
    try
    {
        std::vector<NarrativeConsequenceId> ids = consequence_ids;
        std::sort(ids.begin(), ids.end(), [this](auto a, auto b) {
            const auto pa = consequence_defs_.contains(a) ? consequence_defs_.at(a).priority : 0;
            const auto pb = consequence_defs_.contains(b) ? consequence_defs_.at(b).priority : 0;
            if (pa != pb)
                return pa > pb;
            return a < b;
        });

        std::size_t new_count = 0;
        std::unordered_set<NarrativeConsequenceKey, NarrativeConsequenceKeyHash> new_keys;
        new_keys.reserve(ids.size());
        for (auto id : ids)
        {
            if (!consequence_defs_.contains(id))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.consequence_missing", "consequence missing"));
            const auto key = MakeConsequenceKey(thread, beat, objective, id, correlation);
            if (!consequence_idempotency_.contains(key) && new_keys.insert(key).second)
                ++new_count;
        }
        if (!CanAdvanceRevisionBy(new_count))
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.revision_exhausted", "narrative revision counter is exhausted"));
        const auto generator = consequence_ids_.GetSnapshot();
        if (new_count > 0)
        {
            if (generator.next == 0)
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.id_exhausted", "consequence execution id generator exhausted"));
            const auto available = std::numeric_limits<std::uint64_t>::max() - generator.next + 1;
            if (new_count > available)
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.id_exhausted", "not enough consequence execution ids remain"));
        }

        auto staged_consequences = consequences_;
        auto staged_idempotency = consequence_idempotency_;
        auto staged_generator = consequence_ids_;
        Revision staged_revision = revision_;
        std::vector<NarrativeChange> staged_changes;
        staged_changes.reserve(new_count);
        std::size_t staged_planned = 0;

        for (auto id : ids)
        {
            const auto key = MakeConsequenceKey(thread, beat, objective, id, correlation);
            if (staged_idempotency.contains(key))
            {
                if (consequence_idempotency_.contains(key))
                    ++diagnostics_.idempotency_hits;
                continue;
            }
            const auto next_revision = CheckedNext(staged_revision);
            if (!next_revision)
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.revision_exhausted", "narrative revision counter is exhausted"));
            const auto raw_id = staged_generator.Next();
            if (!raw_id.IsValid())
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.id_exhausted", "consequence execution id generator exhausted"));

            NarrativeConsequenceExecution execution;
            execution.id = NarrativeConsequenceExecutionId{raw_id};
            execution.thread = thread;
            execution.beat = beat;
            execution.objective = objective;
            execution.consequence = id;
            execution.state = ConsequenceExecutionState::Pending;
            execution.planned_at = now;
            execution.correlation = correlation;
            execution.idempotency_key = key;
            execution.revision = *next_revision;
            if (!staged_consequences.emplace(execution.id, execution).second ||
                !staged_idempotency.emplace(key, execution.id).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.duplicate_consequence_execution", "duplicate consequence execution"));
            staged_revision = *next_revision;
            ++staged_planned;
            staged_changes.push_back({0,
                                      NarrativeChangeKind::ConsequencePlanned,
                                      thread,
                                      objective,
                                      beat,
                                      execution.id,
                                      {},
                                      {},
                                      {},
                                      {},
                                      {},
                                      {.time = now, .correlation = correlation},
                                      staged_revision});
        }

        consequences_.swap(staged_consequences);
        consequence_idempotency_.swap(staged_idempotency);
        consequence_ids_ = staged_generator;
        revision_ = staged_revision;
        diagnostics_.consequences_planned += staged_planned;
        for (auto &change : staged_changes)
            Record(std::move(change));
        return foundation::Result<void>::Success();
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to stage narrative consequences"));
    }
}

std::size_t NarrativeService::CountUnplannedConsequences(
    NarrativeThreadId thread, NarrativeBeatId beat, NarrativeObjectiveId objective,
    const std::vector<NarrativeConsequenceId> &consequences, CorrelationId correlation) const
{
    std::size_t count = 0;
    std::unordered_set<NarrativeConsequenceKey, NarrativeConsequenceKeyHash> seen;
    for (auto id : consequences)
    {
        if (!consequence_defs_.contains(id))
            continue;
        const auto key = MakeConsequenceKey(thread, beat, objective, id, correlation);
        if (!consequence_idempotency_.contains(key) && seen.insert(key).second)
            ++count;
    }
    return count;
}

foundation::Result<JournalEntryId> NarrativeService::AddJournalEntry(JournalEntry entry, GameplayContext context)
{
    if (!entry.owner.IsValid() || !IsValid(entry.visibility))
        return foundation::Result<JournalEntryId>::Failure(
            Error("gameplay.narrative.invalid_journal", "invalid journal entry"));
    auto staged_generator = journal_ids_;
    if (!entry.id.IsValid())
    {
        const auto raw = staged_generator.Next();
        if (!raw.IsValid())
            return foundation::Result<JournalEntryId>::Failure(
                Error("gameplay.narrative.id_exhausted", "journal id generator exhausted"));
        entry.id = JournalEntryId{raw};
    }
    if (journal_.contains(entry.id))
        return foundation::Result<JournalEntryId>::Failure(
            Error("gameplay.narrative.duplicate_journal", "duplicate journal entry"));
    AdvanceGeneratorPast(staged_generator, entry.id);
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<JournalEntryId>::Failure(next_revision.GetError());
    entry.discovered_at = context.time;
    entry.revision = next_revision.Value();
    const auto id = entry.id;
    try
    {
        journal_.emplace(id, std::move(entry));
    }
    catch (...)
    {
        return foundation::Result<JournalEntryId>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to add journal entry"));
    }
    journal_ids_ = staged_generator;
    CommitRevision(next_revision.Value());
    const auto &stored = journal_.at(id);
    Record({0, NarrativeChangeKind::JournalEntryAdded, stored.thread, stored.objective, {}, {}, id, {}, {}, {},
            stored.owner, context, revision_});
    return foundation::Result<JournalEntryId>::Success(id);
}

foundation::Result<ClueId> NarrativeService::DiscoverClue(ClueRecord clue, GameplayContext context)
{
    if (!clue.owner.IsValid() || clue.confidence < 0 || clue.confidence > 1'000'000)
        return foundation::Result<ClueId>::Failure(Error("gameplay.narrative.invalid_clue", "invalid clue"));
    auto staged_generator = clue_ids_;
    if (!clue.id.IsValid())
    {
        const auto raw = staged_generator.Next();
        if (!raw.IsValid())
            return foundation::Result<ClueId>::Failure(
                Error("gameplay.narrative.id_exhausted", "clue id generator exhausted"));
        clue.id = ClueId{raw};
    }
    if (clues_.contains(clue.id))
        return foundation::Result<ClueId>::Failure(Error("gameplay.narrative.duplicate_clue", "duplicate clue"));
    AdvanceGeneratorPast(staged_generator, clue.id);
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<ClueId>::Failure(next_revision.GetError());
    clue.discovered_at = context.time;
    clue.revision = next_revision.Value();
    const auto id = clue.id;
    const auto owner = clue.owner;
    try
    {
        clues_.emplace(id, std::move(clue));
    }
    catch (...)
    {
        return foundation::Result<ClueId>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to add clue"));
    }
    clue_ids_ = staged_generator;
    CommitRevision(next_revision.Value());
    Record({0, NarrativeChangeKind::ClueDiscovered, {}, {}, {}, {}, {}, id, {}, {}, owner, context, revision_});
    return foundation::Result<ClueId>::Success(id);
}

foundation::Result<RumorId> NarrativeService::CreateRumor(RumorRecord rumor, GameplayContext context)
{
    if (!rumor.owner_or_scope.IsValid() || !rumor.topic.IsValid() || !IsValid(rumor.state) || rumor.confidence < 0 ||
        rumor.confidence > 1'000'000 || (rumor.expires_at.ticks != 0 && rumor.expires_at < context.time))
        return foundation::Result<RumorId>::Failure(Error("gameplay.narrative.invalid_rumor", "invalid rumor"));
    auto staged_generator = rumor_ids_;
    if (!rumor.id.IsValid())
    {
        const auto raw = staged_generator.Next();
        if (!raw.IsValid())
            return foundation::Result<RumorId>::Failure(
                Error("gameplay.narrative.id_exhausted", "rumor id generator exhausted"));
        rumor.id = RumorId{raw};
    }
    if (rumors_.contains(rumor.id))
        return foundation::Result<RumorId>::Failure(Error("gameplay.narrative.duplicate_rumor", "duplicate rumor"));
    AdvanceGeneratorPast(staged_generator, rumor.id);
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<RumorId>::Failure(next_revision.GetError());
    rumor.created_at = context.time;
    rumor.revision = next_revision.Value();
    const auto id = rumor.id;
    const auto owner = rumor.owner_or_scope;
    try
    {
        auto staged_expiry = rumor_expiry_index_;
        if (rumor.state == RumorState::Active && rumor.expires_at.ticks != 0)
        {
            auto &ids = staged_expiry[rumor.expires_at];
            const auto position = std::lower_bound(ids.begin(), ids.end(), rumor.id);
            if (position == ids.end() || *position != rumor.id)
                ids.insert(position, rumor.id);
        }
        rumors_.emplace(id, std::move(rumor));
        rumor_expiry_index_.swap(staged_expiry);
    }
    catch (...)
    {
        return foundation::Result<RumorId>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to add rumor"));
    }
    rumor_ids_ = staged_generator;
    CommitRevision(next_revision.Value());
    Record({0, NarrativeChangeKind::RumorCreated, {}, {}, {}, {}, {}, {}, id, {}, owner, context, revision_});
    return foundation::Result<RumorId>::Success(id);
}

std::vector<RumorId> NarrativeService::ExpireRumors(GameplayTimePoint now, GameplayContext context)
{
    std::vector<RumorId> expired;
    try
    {
        auto end = rumor_expiry_index_.upper_bound(now);
        for (auto index_it = rumor_expiry_index_.begin(); index_it != end; ++index_it)
            for (auto id : index_it->second)
            {
                auto rumor_it = rumors_.find(id);
                if (rumor_it != rumors_.end() && rumor_it->second.state == RumorState::Active &&
                    rumor_it->second.expires_at.ticks != 0 && rumor_it->second.expires_at <= now)
                    expired.push_back(id);
            }
        std::sort(expired.begin(), expired.end());
        expired.erase(std::unique(expired.begin(), expired.end()), expired.end());
    }
    catch (...)
    {
        return {};
    }
    if (!CanAdvanceRevisionBy(expired.size()))
        return {};
    for (auto id : expired)
    {
        auto next_revision = PrepareRevision();
        if (!next_revision)
            return {};
        auto &rumor = rumors_.at(id);
        rumor.state = RumorState::Expired;
        rumor.revision = next_revision.Value();
        CommitRevision(next_revision.Value());
        auto change_context = context;
        change_context.time = now;
        Record({0, NarrativeChangeKind::RumorExpired, {}, {}, {}, {}, {}, {}, id, {}, rumor.owner_or_scope,
                change_context, revision_});
    }
    auto end = rumor_expiry_index_.upper_bound(now);
    rumor_expiry_index_.erase(rumor_expiry_index_.begin(), end);
    return expired;
}

foundation::Result<NarrativeChoiceId> NarrativeService::CreateChoice(NarrativeChoice choice, GameplayContext context)
{
    if (!choice.actor.IsValid() || !IsValid(choice.state))
        return foundation::Result<NarrativeChoiceId>::Failure(
            Error("gameplay.narrative.invalid_choice", "invalid choice"));
    std::unordered_set<NarrativeChoiceOptionId, IdHash> option_ids;
    try
    {
        option_ids.reserve(choice.options.size());
        for (const auto &option : choice.options)
        {
            if (!option.id.IsValid() || !option_ids.insert(option.id).second)
                return foundation::Result<NarrativeChoiceId>::Failure(
                    Error("gameplay.narrative.invalid_choice", "choice contains invalid or duplicate option id"));
        }
    }
    catch (...)
    {
        return foundation::Result<NarrativeChoiceId>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to validate choice options"));
    }
    auto staged_generator = choice_ids_;
    if (!choice.id.IsValid())
    {
        const auto raw = staged_generator.Next();
        if (!raw.IsValid())
            return foundation::Result<NarrativeChoiceId>::Failure(
                Error("gameplay.narrative.id_exhausted", "choice id generator exhausted"));
        choice.id = NarrativeChoiceId{raw};
    }
    if (choices_.contains(choice.id))
        return foundation::Result<NarrativeChoiceId>::Failure(
            Error("gameplay.narrative.duplicate_choice", "duplicate choice"));
    AdvanceGeneratorPast(staged_generator, choice.id);
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<NarrativeChoiceId>::Failure(next_revision.GetError());
    choice.created_at = context.time;
    choice.revision = next_revision.Value();
    const auto id = choice.id;
    const auto thread = choice.thread;
    const auto actor = choice.actor;
    try
    {
        choices_.emplace(id, std::move(choice));
    }
    catch (...)
    {
        return foundation::Result<NarrativeChoiceId>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to create narrative choice"));
    }
    choice_ids_ = staged_generator;
    CommitRevision(next_revision.Value());
    Record({0, NarrativeChangeKind::ChoiceCreated, thread, {}, {}, {}, {}, {}, {}, id, actor, context, revision_});
    return foundation::Result<NarrativeChoiceId>::Success(id);
}

foundation::Result<void> NarrativeService::ResolveChoice(NarrativeChoiceId id, NarrativeChoiceOptionId option,
                                                         GameplayContext c)
{
    auto it = choices_.find(id);
    if (it == choices_.end())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.choice_missing", "choice missing"));
    if (it->second.state != NarrativeChoiceState::Open)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.choice_closed", "choice already resolved"));
    auto found = std::find_if(it->second.options.begin(), it->second.options.end(),
                              [option](const auto &o) { return o.id == option; });
    if (found == it->second.options.end())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.option_missing", "option missing"));
    NarrativeEvent event;
    event.instigator = it->second.actor;
    event.time = c.time;
    event.correlation = c.correlation;
    NarrativeEvaluationContext ctx{event, it->second.actor, c.time};
    if (!AllSatisfied(found->availability_conditions, ctx))
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.option_unavailable", "choice option unavailable"));
    if (evaluation_budget_exhausted_)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.condition_budget_exhausted", "choice condition evaluation budget exhausted"));
    std::size_t required = 0;
    try
    {
        required = CountUnplannedConsequences(it->second.thread, {}, {}, found->consequences, c.correlation);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to validate choice consequences"));
    }
    if (!CanAdvanceRevisionBy(required + 1))
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.revision_exhausted", "narrative revision counter is exhausted"));
    auto planned = PlanConsequences(it->second.thread, {}, {}, found->consequences, c.correlation, c.time);
    if (!planned)
        return planned;
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    it->second.state = NarrativeChoiceState::Resolved;
    it->second.selected_option = option;
    it->second.closed_at = c.time;
    it->second.revision = next_revision.Value();
    CommitRevision(next_revision.Value());
    Record({0,
            NarrativeChangeKind::ChoiceResolved,
            it->second.thread,
            {},
            {},
            {},
            {},
            {},
            {},
            id,
            it->second.actor,
            c,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> NarrativeService::CancelChoice(NarrativeChoiceId id, GameplayContext c)
{
    auto it = choices_.find(id);
    if (it == choices_.end())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.choice_missing", "choice missing"));
    if (it->second.state == NarrativeChoiceState::Cancelled)
        return foundation::Result<void>::Success();
    if (it->second.state != NarrativeChoiceState::Open)
        return foundation::Result<void>::Failure(Error("gameplay.narrative.choice_closed", "choice is closed"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    it->second.state = NarrativeChoiceState::Cancelled;
    it->second.closed_at = c.time;
    it->second.revision = next_revision.Value();
    CommitRevision(next_revision.Value());
    Record({0, NarrativeChangeKind::ChoiceCancelled, it->second.thread, {}, {}, {}, {}, {}, {}, id,
            it->second.actor, c, revision_});
    return foundation::Result<void>::Success();
}

std::vector<NarrativeChoiceId> NarrativeService::ExpireChoices(GameplayTimePoint now, GameplayContext context)
{
    std::vector<NarrativeChoiceId> ids;
    try
    {
        ids.reserve(choices_.size());
        for (const auto &[id, choice] : choices_)
            if (choice.state == NarrativeChoiceState::Open && choice.expires_at.ticks != 0 && choice.expires_at <= now)
                ids.push_back(id);
        std::sort(ids.begin(), ids.end());
    }
    catch (...)
    {
        return {};
    }
    if (!CanAdvanceRevisionBy(ids.size()))
        return {};
    for (auto id : ids)
    {
        auto next_revision = PrepareRevision();
        if (!next_revision)
            return {};
        auto &choice = choices_.at(id);
        choice.state = NarrativeChoiceState::Expired;
        choice.closed_at = now;
        choice.revision = next_revision.Value();
        CommitRevision(next_revision.Value());
        auto change_context = context;
        change_context.time = now;
        Record({0, NarrativeChangeKind::ChoiceExpired, choice.thread, {}, {}, {}, {}, {}, {}, id, choice.actor,
                change_context, revision_});
    }
    return ids;
}

bool NarrativeService::IsBuiltinAddJournal(NarrativeConsequenceTypeId type) const noexcept
{
    return type.value == TypeId::FromString("narrative.add_journal");
}
bool NarrativeService::IsBuiltinCreateRumor(NarrativeConsequenceTypeId type) const noexcept
{
    return type.value == TypeId::FromString("narrative.create_rumor");
}
void NarrativeService::IndexRumorExpiry(const RumorRecord &rumor)
{
    if (rumor.state != RumorState::Active || rumor.expires_at.ticks == 0)
        return;
    auto &ids = rumor_expiry_index_[rumor.expires_at];
    const auto position = std::lower_bound(ids.begin(), ids.end(), rumor.id);
    if (position == ids.end() || *position != rumor.id)
        ids.insert(position, rumor.id);
}
std::vector<NarrativeConsequenceExecutionId> NarrativeService::ExecutePendingConsequences(
    NarrativeExecutionContext context, std::size_t budget)
{
    std::vector<NarrativeConsequenceExecutionId> applied;
    std::vector<NarrativeConsequenceExecutionId> ids;
    try
    {
        ids.reserve(consequences_.size());
        for (const auto &[id, execution] : consequences_)
            if (execution.state == ConsequenceExecutionState::Pending ||
                execution.state == ConsequenceExecutionState::Deferred ||
                execution.state == ConsequenceExecutionState::FailedRetryable)
                ids.push_back(id);
        std::sort(ids.begin(), ids.end(), [this](auto a, auto b) {
            const auto &left = consequences_.at(a);
            const auto &right = consequences_.at(b);
            const auto left_def = consequence_defs_.find(left.consequence);
            const auto right_def = consequence_defs_.find(right.consequence);
            const auto left_priority = left_def == consequence_defs_.end() ? 0 : left_def->second.priority;
            const auto right_priority = right_def == consequence_defs_.end() ? 0 : right_def->second.priority;
            if (left_priority != right_priority)
                return left_priority > right_priority;
            return a < b;
        });
        if (ids.size() > budget)
        {
            ids.resize(budget);
            ++diagnostics_.budget_exhaustions;
        }
        applied.reserve(ids.size());
    }
    catch (...)
    {
        return {};
    }

    for (auto id : ids)
    {
        auto execution_it = consequences_.find(id);
        if (execution_it == consequences_.end())
            continue;
        auto def_it = consequence_defs_.find(execution_it->second.consequence);
        if (def_it == consequence_defs_.end())
            continue;

        const bool nested_journal = IsBuiltinAddJournal(def_it->second.type) && def_it->second.journal_template.has_value();
        const bool nested_rumor = IsBuiltinCreateRumor(def_it->second.type) && def_it->second.rumor_template.has_value();
        if (!CanAdvanceRevisionBy((nested_journal || nested_rumor) ? 2u : 1u))
            break;

        NarrativeConsequenceExecution staged_execution = execution_it->second;
        if (staged_execution.attempts == std::numeric_limits<std::uint32_t>::max())
        {
            auto next_revision = PrepareRevision();
            if (!next_revision)
                break;
            staged_execution.state = ConsequenceExecutionState::FailedPermanent;
            staged_execution.revision = next_revision.Value();
            execution_it->second = staged_execution;
            CommitRevision(next_revision.Value());
            ++diagnostics_.consequences_failed;
            Record({0, NarrativeChangeKind::ConsequenceFailed, staged_execution.thread, staged_execution.objective,
                    staged_execution.beat, id, {}, {}, {}, {}, context.default_owner,
                    {.time = context.now, .correlation = staged_execution.correlation, .actor = context.default_owner},
                    revision_});
            continue;
        }
        ++staged_execution.attempts;

        NarrativeConsequenceResult result;
        if (IsBuiltinAddJournal(def_it->second.type))
        {
            if (!def_it->second.journal_template)
                result.state = ConsequenceExecutionState::Unsupported;
            else
            {
                const auto &data = *def_it->second.journal_template;
                JournalEntry entry;
                entry.owner = context.default_owner;
                entry.thread = staged_execution.thread;
                entry.objective = staged_execution.objective;
                entry.type = data.type;
                entry.visibility = data.visibility;
                entry.tags = data.tags;
                entry.payload = data.payload;
                auto added = AddJournalEntry(
                    std::move(entry),
                    {.time = context.now, .correlation = staged_execution.correlation, .actor = context.default_owner});
                result.state = added ? ConsequenceExecutionState::Applied : ConsequenceExecutionState::FailedRetryable;
            }
        }
        else if (IsBuiltinCreateRumor(def_it->second.type))
        {
            if (!def_it->second.rumor_template)
                result.state = ConsequenceExecutionState::Unsupported;
            else
            {
                const auto &data = *def_it->second.rumor_template;
                RumorRecord rumor;
                rumor.owner_or_scope = context.default_owner;
                rumor.topic = data.topic;
                rumor.confidence = data.confidence;
                rumor.expires_at = data.lifetime.ticks > 0 ? SaturatingAdd(context.now, data.lifetime)
                                                          : GameplayTimePoint{};
                rumor.payload = data.payload;
                auto created = CreateRumor(
                    std::move(rumor),
                    {.time = context.now, .correlation = staged_execution.correlation, .actor = context.default_owner});
                result.state = created ? ConsequenceExecutionState::Applied : ConsequenceExecutionState::FailedRetryable;
            }
        }
        else if (auto handler = consequence_handlers_.find(def_it->second.type); handler != consequence_handlers_.end())
        {
            try
            {
                result = handler->second->Execute(def_it->second, staged_execution, context);
            }
            catch (...)
            {
                result.state = ConsequenceExecutionState::FailedRetryable;
            }
        }
        else
            result.state = ConsequenceExecutionState::Unsupported;

        if (!IsValid(result.state) || result.state == ConsequenceExecutionState::Pending)
            result.state = ConsequenceExecutionState::FailedPermanent;
        staged_execution.state = result.state == ConsequenceExecutionState::AlreadyApplied
                                     ? ConsequenceExecutionState::Applied
                                     : result.state;
        if (staged_execution.state == ConsequenceExecutionState::Applied)
            staged_execution.applied_at = context.now;

        auto next_revision = PrepareRevision();
        if (!next_revision)
            break;
        staged_execution.revision = next_revision.Value();
        execution_it->second = staged_execution;
        CommitRevision(next_revision.Value());

        if (staged_execution.state == ConsequenceExecutionState::Applied)
        {
            ++diagnostics_.consequences_applied;
            applied.push_back(id);
            Record({0, NarrativeChangeKind::ConsequenceApplied, staged_execution.thread, staged_execution.objective,
                    staged_execution.beat, id, {}, {}, {}, {}, context.default_owner,
                    {.time = context.now, .correlation = staged_execution.correlation, .actor = context.default_owner},
                    revision_});
        }
        else if (staged_execution.state == ConsequenceExecutionState::FailedPermanent ||
                 staged_execution.state == ConsequenceExecutionState::Unsupported)
        {
            ++diagnostics_.consequences_failed;
            Record({0, NarrativeChangeKind::ConsequenceFailed, staged_execution.thread, staged_execution.objective,
                    staged_execution.beat, id, {}, {}, {}, {}, context.default_owner,
                    {.time = context.now, .correlation = staged_execution.correlation, .actor = context.default_owner},
                    revision_});
        }
    }
    return applied;
}

foundation::Result<void> NarrativeService::SetFlag(NarrativeFlag flag, GameplayContext c)
{
    if (!flag.id.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_flag", "invalid flag"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    flag.changed_at = c.time;
    flag.revision = next_revision.Value();
    try
    {
        flags_.insert_or_assign(flag.id, flag);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to set narrative flag"));
    }
    CommitRevision(next_revision.Value());
    Record({0, NarrativeChangeKind::FlagChanged, {}, {}, {}, {}, {}, {}, {}, {}, flag.scope, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NarrativeService::SetVariable(NarrativeVariable variable, GameplayContext c)
{
    if (!variable.id.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.narrative.invalid_variable", "invalid variable"));
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    variable.revision = next_revision.Value();
    try
    {
        variables_.insert_or_assign(variable.id, variable);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to set narrative variable"));
    }
    CommitRevision(next_revision.Value());
    Record({0, NarrativeChangeKind::VariableChanged, {}, {}, {}, {}, {}, {}, {}, {}, variable.scope, c, revision_});
    return foundation::Result<void>::Success();
}

const NarrativeThreadState *NarrativeService::GetThreadState(NarrativeThreadId id) const noexcept
{
    auto it = threads_.find(id);
    return it == threads_.end() ? nullptr : &it->second;
}
const NarrativeObjectiveState *NarrativeService::GetObjectiveState(NarrativeObjectiveId id) const noexcept
{
    auto it = objectives_.find(id);
    return it == objectives_.end() ? nullptr : &it->second;
}
const JournalEntry *NarrativeService::GetJournalEntry(JournalEntryId id) const noexcept
{
    auto it = journal_.find(id);
    return it == journal_.end() ? nullptr : &it->second;
}
const ClueRecord *NarrativeService::GetClue(ClueId id) const noexcept
{
    auto it = clues_.find(id);
    return it == clues_.end() ? nullptr : &it->second;
}
const RumorRecord *NarrativeService::GetRumor(RumorId id) const noexcept
{
    auto it = rumors_.find(id);
    return it == rumors_.end() ? nullptr : &it->second;
}
const NarrativeChoice *NarrativeService::GetChoice(NarrativeChoiceId id) const noexcept
{
    auto it = choices_.find(id);
    return it == choices_.end() ? nullptr : &it->second;
}
std::vector<JournalEntry> NarrativeService::GetJournal(GameplayObjectRef owner) const
{
    std::vector<JournalEntry> out;
    for (const auto &[id, e] : journal_)
    {
        (void)id;
        if (e.owner == owner)
            out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<ClueRecord> NarrativeService::FindClues(GameplayObjectRef owner, TypeId topic) const
{
    std::vector<ClueRecord> out;
    for (const auto &[id, c] : clues_)
    {
        (void)id;
        if (c.owner == owner && (!topic.IsValid() || c.topic == topic))
            out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<RumorRecord> NarrativeService::FindRumors(GameplayObjectRef scope, TypeId topic) const
{
    std::vector<RumorRecord> out;
    for (const auto &[id, r] : rumors_)
    {
        (void)id;
        if (r.owner_or_scope == scope && (!topic.IsValid() || r.topic == topic))
            out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<NarrativeConsequenceExecution> NarrativeService::FindConsequences(ConsequenceExecutionState state) const
{
    std::vector<NarrativeConsequenceExecution> out;
    for (const auto &[id, c] : consequences_)
    {
        (void)id;
        if (c.state == state)
            out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<NarrativeChange> NarrativeService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

NarrativeChangeBatch NarrativeService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    NarrativeChangeBatch batch;
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
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [sequence](const auto &change) { return change.sequence > sequence; });
    return batch;
}

std::uint64_t NarrativeService::CompactTerminalExecutions(GameplayTimePoint before)
{
    bool has_candidate = false;
    for (const auto &[id, execution] : consequences_)
    {
        (void)id;
        const bool terminal = execution.state == ConsequenceExecutionState::Applied ||
                              execution.state == ConsequenceExecutionState::FailedPermanent ||
                              execution.state == ConsequenceExecutionState::Unsupported;
        const auto terminal_time = execution.applied_at.ticks != 0 ? execution.applied_at : execution.planned_at;
        if (terminal && terminal_time < before) { has_candidate = true; break; }
    }
    if (!has_candidate)
        for (const auto &[id, choice] : choices_)
        {
            (void)id;
            if (choice.state != NarrativeChoiceState::Open && choice.closed_at.ticks != 0 && choice.closed_at < before)
            { has_candidate = true; break; }
        }
    if (!has_candidate)
        for (const auto &[id, rumor] : rumors_)
        {
            (void)id;
            if (rumor.state == RumorState::Expired && rumor.expires_at.ticks != 0 && rumor.expires_at < before)
            { has_candidate = true; break; }
        }
    if (!has_candidate)
        for (const auto &[key, execution] : event_executions_)
        {
            (void)key;
            if (execution.state == NarrativeEventExecutionState::Completed && execution.event.time < before)
            { has_candidate = true; break; }
        }
    if (!has_candidate)
        return 0;
    auto next_revision = PrepareRevision();
    if (!next_revision)
        return 0;

    std::uint64_t removed = 0;
    for (auto it = consequences_.begin(); it != consequences_.end();)
    {
        const auto &execution = it->second;
        const bool terminal = execution.state == ConsequenceExecutionState::Applied ||
                              execution.state == ConsequenceExecutionState::FailedPermanent ||
                              execution.state == ConsequenceExecutionState::Unsupported;
        const auto terminal_time = execution.applied_at.ticks != 0 ? execution.applied_at : execution.planned_at;
        if (terminal && terminal_time < before)
        {
            consequence_idempotency_.erase(execution.idempotency_key);
            it = consequences_.erase(it);
            ++removed;
        }
        else ++it;
    }
    for (auto it = choices_.begin(); it != choices_.end();)
    {
        if (it->second.state != NarrativeChoiceState::Open && it->second.closed_at.ticks != 0 &&
            it->second.closed_at < before)
        { it = choices_.erase(it); ++removed; }
        else ++it;
    }
    for (auto it = rumors_.begin(); it != rumors_.end();)
    {
        if (it->second.state == RumorState::Expired && it->second.expires_at.ticks != 0 &&
            it->second.expires_at < before)
        { it = rumors_.erase(it); ++removed; }
        else ++it;
    }
    for (auto it = event_executions_.begin(); it != event_executions_.end();)
    {
        if (it->second.state == NarrativeEventExecutionState::Completed && it->second.event.time < before)
        {
            processed_event_keys_.erase(it->first);
            it = event_executions_.erase(it);
            ++removed;
        }
        else ++it;
    }
    if (removed > 0)
        CommitRevision(next_revision.Value());
    return removed;
}

NarrativeSnapshot NarrativeService::CaptureSnapshot() const
{
    NarrativeSnapshot snapshot;
    for (const auto &[id, value] : threads_)
    {
        (void)id;
        snapshot.thread_states.push_back(value);
    }
    for (const auto &[id, value] : objectives_)
    {
        (void)id;
        snapshot.objective_states.push_back(value);
    }
    for (const auto &[id, value] : consequences_)
    {
        (void)id;
        snapshot.consequences.push_back(value);
    }
    for (const auto &[id, value] : journal_)
    {
        (void)id;
        snapshot.journal_entries.push_back(value);
    }
    for (const auto &[id, value] : clues_)
    {
        (void)id;
        snapshot.clues.push_back(value);
    }
    for (const auto &[id, value] : rumors_)
    {
        (void)id;
        snapshot.rumors.push_back(value);
    }
    for (const auto &[id, value] : flags_)
    {
        (void)id;
        snapshot.flags.push_back(value);
    }
    for (const auto &[id, value] : variables_)
    {
        (void)id;
        snapshot.variables.push_back(value);
    }
    for (const auto &[id, value] : choices_)
    {
        (void)id;
        snapshot.choices.push_back(value);
    }
    for (const auto &[id, value] : storylet_runtime_)
    {
        (void)id;
        snapshot.storylet_runtime.push_back(value);
    }
    for (const auto &[key, value] : event_executions_)
    {
        (void)key;
        snapshot.event_executions.push_back(value);
    }
    snapshot.activated_beats = activated_beats_;
    snapshot.processed_event_keys.assign(processed_event_keys_.begin(), processed_event_keys_.end());

    std::sort(snapshot.thread_states.begin(), snapshot.thread_states.end(),
              [](const auto &a, const auto &b) { return a.thread < b.thread; });
    std::sort(snapshot.objective_states.begin(), snapshot.objective_states.end(),
              [](const auto &a, const auto &b) { return a.objective < b.objective; });
    std::sort(snapshot.consequences.begin(), snapshot.consequences.end(), [](const auto &a, const auto &b) {
        return a.id < b.id;
    });
    std::sort(snapshot.journal_entries.begin(), snapshot.journal_entries.end(), [](const auto &a, const auto &b) {
        return a.id < b.id;
    });
    std::sort(snapshot.clues.begin(), snapshot.clues.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.rumors.begin(), snapshot.rumors.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.flags.begin(), snapshot.flags.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.variables.begin(), snapshot.variables.end(), [](const auto &a, const auto &b) {
        return a.id < b.id;
    });
    std::sort(snapshot.choices.begin(), snapshot.choices.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.storylet_runtime.begin(), snapshot.storylet_runtime.end(), [](const auto &a, const auto &b) {
        return a.id < b.id;
    });
    std::sort(snapshot.event_executions.begin(), snapshot.event_executions.end(), [](const auto &a, const auto &b) {
        return a.key < b.key;
    });
    std::sort(snapshot.activated_beats.begin(), snapshot.activated_beats.end());
    snapshot.activated_beats.erase(std::unique(snapshot.activated_beats.begin(), snapshot.activated_beats.end()),
                                   snapshot.activated_beats.end());
    std::sort(snapshot.processed_event_keys.begin(), snapshot.processed_event_keys.end());
    snapshot.journal_ids = journal_ids_.GetSnapshot();
    snapshot.clue_ids = clue_ids_.GetSnapshot();
    snapshot.rumor_ids = rumor_ids_.GetSnapshot();
    snapshot.choice_ids = choice_ids_.GetSnapshot();
    snapshot.consequence_ids = consequence_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.frozen = frozen_;
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> NarrativeService::RestoreSnapshot(NarrativeSnapshot snapshot)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.restore_requires_frozen_definitions",
                  "register and freeze current-build narrative definitions before restore"));
    const auto current_definition_revision = revision_;

    try
    {
    using ThreadMap = decltype(threads_);
    using ObjectiveMap = decltype(objectives_);
    using ConsequenceMap = decltype(consequences_);
    using JournalMap = decltype(journal_);
    using ClueMap = decltype(clues_);
    using RumorMap = decltype(rumors_);
    using RumorExpiryIndex = decltype(rumor_expiry_index_);
    using FlagMap = decltype(flags_);
    using VariableMap = decltype(variables_);
    using ChoiceMap = decltype(choices_);
    using StoryletMap = decltype(storylet_runtime_);
    using ConsequenceKeyMap = decltype(consequence_idempotency_);
    using EventMap = decltype(event_executions_);

    ThreadMap threads;
    ObjectiveMap objectives;
    ConsequenceMap consequences;
    JournalMap journal;
    ClueMap clues;
    RumorMap rumors;
    RumorExpiryIndex rumor_expiry_index;
    FlagMap flags;
    VariableMap variables;
    ChoiceMap choices;
    StoryletMap storylets;
    ConsequenceKeyMap consequence_keys;
    EventMap event_executions;
    std::unordered_set<NarrativeEventKey, NarrativeEventKeyHash> processed_keys;
    std::vector<NarrativeBeatId> activated_beats;

    auto duplicate = []() {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.restore_duplicate", "snapshot contains duplicate narrative identity"));
    };

    for (const auto &value : snapshot.thread_states)
    {
        if (!value.thread.IsValid() || !thread_defs_.contains(value.thread) || !IsValid(value.state) ||
            value.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "thread state references missing current definition"));
        if (!threads.emplace(value.thread, value).second)
            return duplicate();
    }
    for (const auto &value : snapshot.objective_states)
    {
        if (!value.objective.IsValid() || !objective_defs_.contains(value.objective) || !IsValid(value.state) ||
            value.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "objective state references missing current definition"));
        if (!objectives.emplace(value.objective, value).second)
            return duplicate();
    }
    for (const auto &value : snapshot.consequences)
    {
        if (!value.id.IsValid() || !value.consequence.IsValid() || !consequence_defs_.contains(value.consequence) ||
            !IsValid(value.state) || value.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "consequence execution references missing definition"));
        if (value.thread.IsValid() && !thread_defs_.contains(value.thread))
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "consequence references missing thread"));
        if (value.beat.IsValid() && !beat_defs_.contains(value.beat))
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "consequence references missing beat"));
        if (value.objective.IsValid() && !objective_defs_.contains(value.objective))
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "consequence references missing objective"));
        if (!consequences.emplace(value.id, value).second)
            return duplicate();
        if (value.idempotency_key.IsValid() && !consequence_keys.emplace(value.idempotency_key, value.id).second)
            return duplicate();
    }
    for (const auto &value : snapshot.journal_entries)
    {
        if (!value.id.IsValid() || !IsValid(value.visibility) || value.revision.value > snapshot.revision.value ||
            (value.thread.IsValid() && !thread_defs_.contains(value.thread)) ||
            (value.objective.IsValid() && !objective_defs_.contains(value.objective)))
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "invalid journal entry snapshot"));
        if (!journal.emplace(value.id, value).second)
            return duplicate();
    }
    for (const auto &value : snapshot.clues)
    {
        if (!value.id.IsValid() || value.revision.value > snapshot.revision.value ||
            !clues.emplace(value.id, value).second)
            return foundation::Result<void>::Failure(Error("gameplay.narrative.restore_invalid", "invalid clue snapshot"));
    }
    for (const auto &value : snapshot.rumors)
    {
        if (!value.id.IsValid() || !IsValid(value.state) || value.revision.value > snapshot.revision.value ||
            !rumors.emplace(value.id, value).second)
            return foundation::Result<void>::Failure(Error("gameplay.narrative.restore_invalid", "invalid rumor snapshot"));
    }
    for (const auto &[id, value] : rumors)
    {
        if (value.state == RumorState::Active && value.expires_at.ticks != 0)
            rumor_expiry_index[value.expires_at].push_back(id);
    }
    for (auto &[time, ids] : rumor_expiry_index)
    {
        (void)time;
        std::sort(ids.begin(), ids.end());
    }
    for (const auto &value : snapshot.flags)
    {
        if (!value.id.IsValid() || value.revision.value > snapshot.revision.value ||
            !flags.emplace(value.id, value).second)
            return foundation::Result<void>::Failure(Error("gameplay.narrative.restore_invalid", "invalid flag snapshot"));
    }
    for (const auto &value : snapshot.variables)
    {
        if (!value.id.IsValid() || value.revision.value > snapshot.revision.value ||
            !variables.emplace(value.id, value).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "invalid variable snapshot"));
    }
    for (const auto &value : snapshot.choices)
    {
        if (!value.id.IsValid() || !IsValid(value.state) || value.revision.value > snapshot.revision.value ||
            (value.thread.IsValid() && !thread_defs_.contains(value.thread)) ||
            !choices.emplace(value.id, value).second)
            return foundation::Result<void>::Failure(Error("gameplay.narrative.restore_invalid", "invalid choice snapshot"));
    }
    for (const auto &value : snapshot.storylet_runtime)
    {
        if (!value.id.IsValid() || !storylet_defs_.contains(value.id) ||
            value.revision.value > snapshot.revision.value || !storylets.emplace(value.id, value).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "storylet runtime references missing definition"));
    }
    for (const auto &[id, definition] : storylet_defs_)
    {
        (void)definition;
        if (!storylets.contains(id))
            storylets.emplace(id, StoryletRuntimeState{.id = id, .revision = snapshot.revision});
    }

    for (auto beat : snapshot.activated_beats)
    {
        if (!beat.IsValid() || !beat_defs_.contains(beat))
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "activated beat references missing definition"));
        if (std::find(activated_beats.begin(), activated_beats.end(), beat) != activated_beats.end())
            return duplicate();
        activated_beats.push_back(beat);
    }

    for (const auto &value : snapshot.event_executions)
    {
        if (value.key != MakeEventKey(value.event) || !IsValid(value.state) || !IsValid(value.phase) ||
            value.revision.value > snapshot.revision.value || value.next_beat > value.beat_plan.size() ||
            value.next_objective > value.objective_plan.size() || value.next_storylet > value.storylet_plan.size())
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative.restore_invalid", "invalid event execution snapshot"));
        for (auto id : value.beat_plan)
            if (!beat_defs_.contains(id))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.restore_invalid", "event work plan references missing beat"));
        for (auto id : value.objective_plan)
            if (!objective_defs_.contains(id))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.restore_invalid", "event work plan references missing objective"));
        for (auto id : value.storylet_plan)
            if (!storylet_defs_.contains(id))
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative.restore_invalid", "event work plan references missing storylet"));
        if (!event_executions.emplace(value.key, value).second)
            return duplicate();
        if (value.state == NarrativeEventExecutionState::Completed)
            processed_keys.insert(value.key);
    }
    // Backward compatibility for snapshots captured before explicit event execution state existed.
    for (const auto &key : snapshot.processed_event_keys)
    {
        processed_keys.insert(key);
        if (!event_executions.contains(key))
        {
            NarrativeEvent event;
            event.type = key.type;
            event.subject = key.subject;
            event.instigator = key.instigator;
            event.target = key.target;
            event.area = key.area;
            event.time = key.time;
            event.correlation = key.correlation;
            event_executions.emplace(key, NarrativeEventExecution{.key = key,
                                                                  .event = event,
                                                                  .state = NarrativeEventExecutionState::Completed,
                                                                  .phase = NarrativeEventExecutionPhase::Completed,
                                                                  .work_plan_initialized = true,
                                                                  .beat_plan = {},
                                                                  .objective_plan = {},
                                                                  .storylet_plan = {},
                                                                  .revision = snapshot.revision});
        }
    }
    while (processed_keys.size() > kCompletedEventRetention)
    {
        auto victim = event_executions.end();
        for (auto it = event_executions.begin(); it != event_executions.end(); ++it)
        {
            if (it->second.state != NarrativeEventExecutionState::Completed)
                continue;
            if (victim == event_executions.end() || it->second.event.time < victim->second.event.time ||
                (it->second.event.time == victim->second.event.time && it->first < victim->first))
                victim = it;
        }
        if (victim == event_executions.end())
            break;
        processed_keys.erase(victim->first);
        event_executions.erase(victim);
    }

    auto max_low_for_scope = [](const auto &container, IdScopeId scope, auto get_id) {
        std::uint64_t max_low = 0;
        for (const auto &value : container)
        {
            const auto id = get_id(value);
            if (id.value.High() == scope.Raw())
                max_low = std::max(max_low, id.value.Low());
        }
        return max_low;
    };
    const auto journal_max = max_low_for_scope(snapshot.journal_entries, journal_ids_.Scope(),
                                               [](const auto &v) { return v.id; });
    const auto clue_max = max_low_for_scope(snapshot.clues, clue_ids_.Scope(), [](const auto &v) { return v.id; });
    const auto rumor_max = max_low_for_scope(snapshot.rumors, rumor_ids_.Scope(), [](const auto &v) { return v.id; });
    const auto choice_max = max_low_for_scope(snapshot.choices, choice_ids_.Scope(), [](const auto &v) { return v.id; });
    const auto consequence_max = max_low_for_scope(snapshot.consequences, consequence_ids_.Scope(),
                                                   [](const auto &v) { return v.id; });
    if (!ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.journal_ids, journal_ids_.Scope(), journal_max) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.clue_ids, clue_ids_.Scope(), clue_max) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.rumor_ids, rumor_ids_.Scope(), rumor_max) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.choice_ids, choice_ids_.Scope(), choice_max) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.consequence_ids, consequence_ids_.Scope(),
                                                                consequence_max))
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.restore_generator_invalid", "narrative id generator snapshot is invalid"));

    threads_.swap(threads);
    objectives_.swap(objectives);
    consequences_.swap(consequences);
    journal_.swap(journal);
    clues_.swap(clues);
    rumors_.swap(rumors);
    rumor_expiry_index_.swap(rumor_expiry_index);
    flags_.swap(flags);
    variables_.swap(variables);
    choices_.swap(choices);
    storylet_runtime_.swap(storylets);
    consequence_idempotency_.swap(consequence_keys);
    event_executions_.swap(event_executions);
    processed_event_keys_.swap(processed_keys);
    activated_beats_.swap(activated_beats);
    journal_ids_.Restore(snapshot.journal_ids);
    clue_ids_.Restore(snapshot.clue_ids);
    rumor_ids_.Restore(snapshot.rumor_ids);
    choice_ids_.Restore(snapshot.choice_ids);
    consequence_ids_.Restore(snapshot.consequence_ids);
    revision_.value = std::max(current_definition_revision.value, snapshot.revision.value);
    changes_.clear();
    next_change_sequence_ = 1;
    evaluation_counter_ = 0;
    evaluation_limit_ = static_cast<std::size_t>(-1);
    evaluation_depth_ = 0;
    evaluation_budget_exhausted_ = false;
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative.allocation_failed", "failed to stage narrative snapshot restore"));
    }
}

NarrativeDiagnostics NarrativeService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.threads = threads_.size();
    for (const auto &[id, t] : threads_)
    {
        (void)id;
        if (t.state == NarrativeRuntimeState::Active)
            ++d.active_threads;
        if (t.state == NarrativeRuntimeState::Completed)
            ++d.completed_threads;
    }
    d.objectives = objectives_.size();
    for (const auto &[id, o] : objectives_)
    {
        (void)id;
        if (o.state == NarrativeObjectiveRuntimeState::Active)
            ++d.active_objectives;
        if (o.state == NarrativeObjectiveRuntimeState::Completed)
            ++d.completed_objectives;
    }
    d.journal_entries = journal_.size();
    d.clues = clues_.size();
    d.rumors = rumors_.size();
    d.storylets = storylet_defs_.size();
    return d;
}
void NarrativeService::Record(NarrativeChange c) noexcept
{
    if (next_change_sequence_ == 0)
        return;
    c.sequence = next_change_sequence_;
    try
    {
        changes_.push_back(std::move(c));
        if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
            next_change_sequence_ = 0;
        else
            ++next_change_sequence_;
        while (changes_.size() > kChangeJournalCapacity)
            changes_.pop_front();
    }
    catch (...)
    {
        changes_.clear();
        if (const auto next_epoch = CheckedNextChangeEpoch(journal_epoch_))
        {
            journal_epoch_ = *next_epoch;
            next_change_sequence_ = 1;
        }
        else
            next_change_sequence_ = 0;
    }
}
NarrativeEventKey NarrativeService::MakeEventKey(const NarrativeEvent &e) const noexcept
{
    return {.type = e.type,
            .subject = e.subject,
            .instigator = e.instigator,
            .target = e.target,
            .area = e.area,
            .time = e.time,
            .correlation = e.correlation};
}
NarrativeConsequenceKey NarrativeService::MakeConsequenceKey(NarrativeThreadId t, NarrativeBeatId b,
                                                             NarrativeObjectiveId o, NarrativeConsequenceId c,
                                                             CorrelationId corr) const noexcept
{
    return {.thread = t, .beat = b, .objective = o, .consequence = c, .correlation = corr};
}
} // namespace epidemic::gameplay::narrative
