#include "Epidemic/GameFramework/Interaction/interaction.h"
#include <algorithm>
#include <iterator>
#include <limits>

namespace epidemic::gameplay::interaction
{
namespace
{
foundation::Error E(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
InteractionService::InteractionService()
    : ids_(GameplayObjectId::FromString("framework.interaction.execution.seed").High())
{
}
foundation::Result<void> InteractionService::RegisterDefinition(InteractionDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(E("gameplay.registry_frozen", "interaction registry is frozen"));
    if (!d.type.IsValid() || d.canonical_name.empty() || definitions_.contains(d.type))
        return foundation::Result<void>::Failure(
            E("gameplay.interaction.invalid_definition", "invalid or duplicate interaction definition"));
    definitions_.emplace(d.type, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<void> InteractionService::RegisterProvider(const IInteractionProvider &p)
{
    if (frozen_)
        return foundation::Result<void>::Failure(E("gameplay.registry_frozen", "interaction registry is frozen"));
    if (!p.Id().IsValid() || providers_.contains(p.Id()))
        return foundation::Result<void>::Failure(
            E("gameplay.interaction.invalid_provider", "invalid or duplicate interaction provider"));
    providers_.emplace(p.Id(), &p);
    return foundation::Result<void>::Success();
}
foundation::Result<void> InteractionService::RegisterExecutor(InteractionTypeId t, IInteractionExecutor &e)
{
    if (frozen_)
        return foundation::Result<void>::Failure(E("gameplay.registry_frozen", "interaction registry is frozen"));
    if (!t.IsValid() || !definitions_.contains(t) || executors_.contains(t))
        return foundation::Result<void>::Failure(
            E("gameplay.interaction.invalid_executor", "invalid or duplicate interaction executor"));
    executors_.emplace(t, &e);
    return foundation::Result<void>::Success();
}
const InteractionDefinition *InteractionService::FindDefinition(InteractionTypeId t) const noexcept
{
    auto i = definitions_.find(t);
    return i == definitions_.end() ? nullptr : &i->second;
}
std::vector<InteractionCandidate> InteractionService::GetAvailableInteractions(const InteractionContext &c) const
{
    ++candidate_requests_;
    std::vector<InteractionCandidate> out;
    std::vector<std::pair<InteractionProviderId, const IInteractionProvider *>> ps(providers_.begin(),
                                                                                   providers_.end());
    std::sort(ps.begin(), ps.end(), [](auto &a, auto &b) { return a.first < b.first; });
    for (auto [p, provider] : ps)
    {
        std::vector<InteractionCandidate> v;
        try
        {
            v = provider->Collect(c);
        }
        catch (...)
        {
            ++callback_failures_;
            continue;
        }
        for (auto &x : v)
        {
            x.provider = p;
            if (x.actor.IsValid() == false)
                x.actor = c.actor;
            if (x.target.IsValid() == false)
                x.target = c.target;
            const auto definition = definitions_.find(x.type);
            if (definition != definitions_.end() && x.availability == InteractionAvailability::Available)
            {
                InteractionContext candidate_context = c;
                candidate_context.actor = x.actor;
                candidate_context.target = x.target;
                if (MaterializationAllowed(definition->second, candidate_context))
                    out.push_back(std::move(x));
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        if (a.type != b.type)
            return a.type < b.type;
        return a.provider < b.provider;
    });
    std::vector<InteractionCandidate> deduplicated;
    deduplicated.reserve(out.size());
    for (auto &candidate : out)
    {
        const bool duplicate = std::any_of(deduplicated.begin(), deduplicated.end(), [&](const auto &existing) {
            return existing.type == candidate.type && existing.actor == candidate.actor &&
                   existing.target == candidate.target;
        });
        if (!duplicate)
            deduplicated.push_back(std::move(candidate));
    }
    candidates_ += deduplicated.size();
    return deduplicated;
}
bool InteractionService::MaterializationAllowed(const InteractionDefinition &d, const InteractionContext &c) const
{
    if (d.materialization == InteractionMaterializationPolicy::AbstractAllowed)
        return true;
    if (!state_provider_)
        return false;
    bool a = false, t = false;
    try
    {
        a = state_provider_->IsMaterialized(c.actor);
        t = state_provider_->IsMaterialized(c.target);
    }
    catch (...)
    {
        ++callback_failures_;
        return false;
    }
    switch (d.materialization)
    {
    case InteractionMaterializationPolicy::AbstractAllowed:
        return true;
    case InteractionMaterializationPolicy::RequiresMaterializedActor:
        return a;
    case InteractionMaterializationPolicy::RequiresMaterializedTarget:
        return t;
    case InteractionMaterializationPolicy::RequiresBothMaterialized:
        return a && t;
    }
    return false;
}
foundation::Result<InteractionPlan> InteractionService::Prepare(const InteractionContext &c,
                                                                InteractionCandidate candidate) const
{
    if (!frozen_)
        return foundation::Result<InteractionPlan>::Failure(E("gameplay.registry_not_frozen", "interaction registry must be frozen before prepare"));
    auto d = FindDefinition(candidate.type);
    if (!d || candidate.availability != InteractionAvailability::Available)
        return foundation::Result<InteractionPlan>::Failure(
            E("gameplay.interaction.unavailable", "interaction is unavailable"));
    if (!MaterializationAllowed(*d, c))
        return foundation::Result<InteractionPlan>::Failure(
            E("gameplay.interaction.not_materialized", "interaction materialization requirements are not met"));
    if (candidate.actor != c.actor || candidate.target != c.target)
        return foundation::Result<InteractionPlan>::Failure(
            E("gameplay.interaction.context_mismatch", "interaction candidate context mismatch"));
    InteractionContext captured = c;
    if (state_provider_)
    {
        Revision actor_revision{}, target_revision{};
        try
        {
            actor_revision = state_provider_->RevisionOf(c.actor);
            target_revision = state_provider_->RevisionOf(c.target);
        }
        catch (...)
        {
            ++callback_failures_;
            return foundation::Result<InteractionPlan>::Failure(
                E("gameplay.interaction.state_provider_exception", "interaction state provider threw during prepare"));
        }
        if (c.actor_revision.value != 0 && c.actor_revision != actor_revision)
            return foundation::Result<InteractionPlan>::Failure(E("gameplay.stale_revision", "actor revision changed before prepare"));
        if (c.target_revision.value != 0 && c.target_revision != target_revision)
            return foundation::Result<InteractionPlan>::Failure(E("gameplay.stale_revision", "target revision changed before prepare"));
        captured.actor_revision = actor_revision;
        captured.target_revision = target_revision;
    }
    InteractionPlan p{std::move(candidate), captured, revision_};
    auto ex = executors_.find(p.candidate.type);
    if (ex != executors_.end())
    {
        try
        {
            auto v = ex->second->Validate(p);
            if (!v)
                return foundation::Result<InteractionPlan>::Failure(v.GetError());
        }
        catch (...)
        {
            ++callback_failures_;
            return foundation::Result<InteractionPlan>::Failure(
                E("gameplay.interaction.executor_exception", "interaction executor threw during validation"));
        }
    }
    return foundation::Result<InteractionPlan>::Success(std::move(p));
}
foundation::Result<InteractionResult> InteractionService::Commit(const InteractionPlan &p)
{
    if (!frozen_)
        return foundation::Result<InteractionResult>::Failure(E("gameplay.registry_not_frozen", "interaction registry must be frozen before commit"));
    auto d = FindDefinition(p.candidate.type);
    if (!d)
        return foundation::Result<InteractionResult>::Failure(
            E("gameplay.interaction.definition_missing", "interaction definition missing"));

    // Materialization is a volatile precondition and is intentionally checked
    // independently from gameplay revisions. Streaming/runtime representation can
    // change without changing the authoritative gameplay object revision.
    if (!MaterializationAllowed(*d, p.context))
        return foundation::Result<InteractionResult>::Failure(
            E("gameplay.interaction.not_materialized", "interaction materialization requirements changed before commit"));

    if (state_provider_)
    {
        try
        {
            if (p.context.actor_revision.value && state_provider_->RevisionOf(p.context.actor) != p.context.actor_revision)
                return foundation::Result<InteractionResult>::Failure(
                    E("gameplay.stale_revision", "actor revision changed"));
            if (p.context.target_revision.value &&
                state_provider_->RevisionOf(p.context.target) != p.context.target_revision)
                return foundation::Result<InteractionResult>::Failure(
                    E("gameplay.stale_revision", "target revision changed"));
        }
        catch (...)
        {
            ++callback_failures_;
            return foundation::Result<InteractionResult>::Failure(
                E("gameplay.interaction.state_provider_exception", "interaction state provider threw during commit preflight"));
        }
    }
    InteractionExecutionId id{ids_.Next()};
    if (!id.IsValid())
        return foundation::Result<InteractionResult>::Failure(E("gameplay.interaction.id_exhausted", "interaction execution id generator is exhausted"));
    auto ex = executors_.find(d->type);
    if (d->mode == InteractionExecutionMode::Instant)
    {
        if (ex != executors_.end())
        {
            try
            {
                auto v = ex->second->Validate(p);
                if (!v)
                {
                    ++failed_;
                    return foundation::Result<InteractionResult>::Failure(v.GetError());
                }
            }
            catch (...)
            {
                ++callback_failures_;
                ++failed_;
                return foundation::Result<InteractionResult>::Failure(
                    E("gameplay.interaction.executor_exception", "interaction executor threw during commit validation"));
            }
            auto r = ex->second->Commit(p, id);
            if (!r)
            {
                ++failed_;
                return foundation::Result<InteractionResult>::Failure(r.GetError());
            }
        }
        ++completed_;
        Bump();
        Record({0, InteractionChangeKind::Completed, id, d->type, p.context.actor, p.context.target, p.context.gameplay,
                revision_});
        return foundation::Result<InteractionResult>::Success({id, InteractionSessionState::Completed, {}});
    }
    InteractionSession s{id,
                         d->type,
                         p.context.actor,
                         p.context.target,
                         InteractionSessionState::Active,
                         p.context.gameplay.time,
                         std::nullopt,
                         p.context.actor_revision,
                         p.context.target_revision,
                         p.candidate.payload,
                         p.context.gameplay,
                         std::nullopt,
                         {}};
    if (d->mode == InteractionExecutionMode::Timed && d->duration.ticks > 0)
    {
        const auto due = ::epidemic::gameplay::CheckedAdd(p.context.gameplay.time, d->duration);
        if (!due.has_value())
            return foundation::Result<InteractionResult>::Failure(E("gameplay.time_overflow", "interaction completion time overflows"));
        s.completes_at = *due;
    }
    Bump();
    s.revision = revision_;
    sessions_.emplace(id, s);
    Record({0, InteractionChangeKind::Started, id, d->type, s.actor, s.target, p.context.gameplay, revision_});
    return foundation::Result<InteractionResult>::Success({id, InteractionSessionState::Active, {}});
}
foundation::Result<void> InteractionService::BindCompletionSchedule(InteractionExecutionId id, ScheduleId schedule)
{
    auto i = sessions_.find(id);
    if (i == sessions_.end() || i->second.state != InteractionSessionState::Active || !schedule.IsValid())
        return foundation::Result<void>::Failure(
            E("gameplay.interaction.session_missing", "active interaction session missing or schedule invalid"));
    if (i->second.completion_schedule.has_value())
    {
        if (*i->second.completion_schedule == schedule) return foundation::Result<void>::Success();
        return foundation::Result<void>::Failure(E("gameplay.interaction.schedule_rebind", "interaction already has a completion schedule"));
    }
    const auto occupied = session_by_schedule_.find(schedule);
    if (occupied != session_by_schedule_.end() && occupied->second != id)
        return foundation::Result<void>::Failure(E("gameplay.interaction.schedule_conflict", "completion schedule is already bound"));
    i->second.completion_schedule = schedule;
    session_by_schedule_[schedule] = id;
    Bump();
    i->second.revision = revision_;
    return foundation::Result<void>::Success();
}
foundation::Result<void> InteractionService::Complete(InteractionExecutionId id, GameplayContext c)
{
    auto i = sessions_.find(id);
    if (i == sessions_.end() || i->second.state != InteractionSessionState::Active)
        return foundation::Result<void>::Failure(E("gameplay.interaction.session_missing", "active interaction session missing"));
    const auto snapshot = i->second;
    const auto* d = FindDefinition(snapshot.type);
    if (!d)
        return foundation::Result<void>::Failure(E("gameplay.interaction.definition_missing", "interaction definition missing"));

    InteractionCandidate candidate;
    candidate.type = snapshot.type;
    candidate.actor = snapshot.actor;
    candidate.target = snapshot.target;
    candidate.payload = snapshot.payload;
    InteractionContext context;
    context.actor = snapshot.actor;
    context.target = snapshot.target;
    context.actor_revision = snapshot.actor_revision;
    context.target_revision = snapshot.target_revision;
    context.gameplay = snapshot.origin_context;
    if (c.time.ticks != 0) context.gameplay.time = c.time;
    if (c.tick.value != 0) context.gameplay.tick = c.tick;

    auto fail_terminal = [&](TypeId reason) -> foundation::Result<void> {
        Bump();
        ++failed_;
        if (snapshot.completion_schedule.has_value()) session_by_schedule_.erase(*snapshot.completion_schedule);
        sessions_.erase(id);
        InteractionChange change{0, InteractionChangeKind::Failed, id, snapshot.type, snapshot.actor, snapshot.target,
                                 context.gameplay, revision_};
        change.reason = reason;
        change.completion_schedule = snapshot.completion_schedule;
        Record(std::move(change));
        return foundation::Result<void>::Failure(E("gameplay.interaction.failed", "interaction completion failed"));
    };

    if (!MaterializationAllowed(*d, context))
        return fail_terminal(TypeId::FromString("gameplay.interaction.not_materialized"));
    if (state_provider_)
    {
        try
        {
            if (state_provider_->RevisionOf(snapshot.actor) != snapshot.actor_revision ||
                state_provider_->RevisionOf(snapshot.target) != snapshot.target_revision)
                return fail_terminal(TypeId::FromString("gameplay.stale_revision"));
        }
        catch (...)
        {
            ++callback_failures_;
            return fail_terminal(TypeId::FromString("gameplay.interaction.state_provider_exception"));
        }
    }

    InteractionPlan plan{std::move(candidate), context, revision_};
    const auto ex = executors_.find(snapshot.type);
    if (ex != executors_.end())
    {
        try
        {
            if (!ex->second->Validate(plan))
                return fail_terminal(TypeId::FromString("gameplay.interaction.executor_validation_failed"));
        }
        catch (...)
        {
            ++callback_failures_;
            return fail_terminal(TypeId::FromString("gameplay.interaction.executor_exception"));
        }
        if (!ex->second->Commit(plan, id))
            return fail_terminal(TypeId::FromString("gameplay.interaction.executor_commit_failed"));
    }

    Bump();
    ++completed_;
    if (snapshot.completion_schedule.has_value()) session_by_schedule_.erase(*snapshot.completion_schedule);
    sessions_.erase(id);
    InteractionChange completed_change{0, InteractionChangeKind::Completed, id, snapshot.type, snapshot.actor,
                                       snapshot.target, context.gameplay, revision_};
    completed_change.completion_schedule = snapshot.completion_schedule;
    Record(std::move(completed_change));
    return foundation::Result<void>::Success();
}

foundation::Result<void> InteractionService::Cancel(InteractionExecutionId id, TypeId reason, GameplayContext c)
{
    auto i = sessions_.find(id);
    if (i == sessions_.end() || i->second.state != InteractionSessionState::Active)
        return foundation::Result<void>::Failure(E("gameplay.interaction.session_missing", "active interaction session missing"));
    const auto snapshot = i->second;
    Bump();
    ++cancelled_;
    if (snapshot.completion_schedule.has_value()) session_by_schedule_.erase(*snapshot.completion_schedule);
    sessions_.erase(i);
    InteractionChange change{0, InteractionChangeKind::Cancelled, id, snapshot.type, snapshot.actor, snapshot.target, c, revision_};
    change.reason = reason;
    change.completion_schedule = snapshot.completion_schedule;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

foundation::Result<void> InteractionService::SweepTimed(GameplayTimePoint, GameplayContext)
{
    return foundation::Result<void>::Failure(E("gameplay.interaction.external_time_required",
                                                "timed interactions are completed exclusively through the Time integration"));
}

const InteractionSession *InteractionService::FindSession(InteractionExecutionId id) const noexcept
{
    auto i = sessions_.find(id);
    return i == sessions_.end() ? nullptr : &i->second;
}
std::optional<InteractionSession> InteractionService::FindSessionCopy(InteractionExecutionId id) const noexcept
{
    const auto* session = FindSession(id);
    return session == nullptr ? std::nullopt : std::optional<InteractionSession>{*session};
}
std::vector<InteractionSession> InteractionService::FindActive(GameplayObjectRef actor) const
{
    std::vector<InteractionSession> out;
    for (const auto &[id, s] : sessions_)
    {
        (void)id;
        if (s.actor == actor && s.state == InteractionSessionState::Active)
            out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<InteractionSession> InteractionService::SessionsRequiringSchedule() const
{
    std::vector<InteractionSession> out;
    for (const auto &[id, session] : sessions_)
    {
        (void)id;
        const auto* definition = FindDefinition(session.type);
        if (session.state == InteractionSessionState::Active && definition &&
            definition->mode == InteractionExecutionMode::Timed && session.completes_at.has_value() &&
            !session.completion_schedule.has_value())
            out.push_back(session);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return out;
}
InteractionSnapshot InteractionService::CaptureSnapshot() const
{
    InteractionSnapshot s;
    for (const auto &[id, x] : sessions_)
    {
        (void)id;
        auto d = FindDefinition(x.type);
        if (d && d->persistence == InteractionPersistence::PersistentSession &&
            x.state == InteractionSessionState::Active)
        {
            auto persistent = x;
            persistent.completion_schedule.reset();
            s.sessions.push_back(std::move(persistent));
        }
    }
    std::sort(s.sessions.begin(), s.sessions.end(), [](auto &a, auto &b) { return a.id < b.id; });
    s.ids = ids_.GetSnapshot();
    s.revision = revision_;
    s.change_epoch = journal_epoch_;
    return s;
}
foundation::Result<void> InteractionService::RestoreSnapshot(InteractionSnapshot s)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(s.change_epoch > journal_epoch_ ? s.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    if (!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(s.ids) || s.ids.scope != ids_.Scope().Raw())
        return foundation::Result<void>::Failure(
            E("gameplay.interaction.restore_invalid_generator", "invalid interaction execution id generator snapshot"));

    std::unordered_map<InteractionExecutionId, InteractionSession, ExecutionHash> rebuilt;
    std::uint64_t max_low = 0;
    for (auto &x : s.sessions)
    {
        const auto* d = FindDefinition(x.type);
        if (!x.id.IsValid() || !x.actor.IsValid() || !x.target.IsValid() || !d ||
            d->persistence != InteractionPersistence::PersistentSession || x.state != InteractionSessionState::Active ||
            x.revision.value > s.revision.value || rebuilt.contains(x.id) || x.id.value.High() != s.ids.scope)
            return foundation::Result<void>::Failure(E("gameplay.interaction.restore_invalid", "invalid session in snapshot"));
        max_low = std::max(max_low, x.id.value.Low());
        x.completion_schedule.reset();
        rebuilt.emplace(x.id, std::move(x));
    }
    if (max_low != 0 && s.ids.next != 0 && s.ids.next <= max_low)
        return foundation::Result<void>::Failure(
            E("gameplay.interaction.restore_invalid_generator", "interaction execution id generator is behind restored sessions"));

    sessions_ = std::move(rebuilt);
    session_by_schedule_.clear();
    ids_.Restore(s.ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

std::vector<InteractionChange> InteractionService::ChangesSinceSequence(std::uint64_t x) const
{
    return ReadChangesSinceSequence(x).changes;
}

InteractionChangeBatch InteractionService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    InteractionChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (!changes_.empty() && sequence < changes_.front().sequence - 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    const auto found = std::upper_bound(changes_.begin(), changes_.end(), sequence,
                                        [](std::uint64_t value, const InteractionChange& change) {
                                            return value < change.sequence;
                                        });
    batch.changes.assign(found, changes_.end());
    return batch;
}

void InteractionService::Record(InteractionChange c)
{
    if (next_change_sequence_ == 0)
        return;
    c.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(c));
    while (changes_.size() > kChangeJournalCapacity)
    {
        changes_.pop_front();
        ++dropped_changes_;
    }
}
InteractionDiagnostics InteractionService::GetDiagnostics() const noexcept
{
    return {candidate_requests_, candidates_, sessions_.size(), completed_, cancelled_, failed_, callback_failures_,
            dropped_changes_};
}
} // namespace epidemic::gameplay::interaction




