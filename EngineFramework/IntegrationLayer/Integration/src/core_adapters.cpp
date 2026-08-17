#include "Epidemic/GameFramework/Integration/core_adapters.h"

#include <algorithm>

namespace epidemic::gameplay::integration
{
foundation::Result<void> FactsQueryAdapter::RegisterProviders()
{
    const queries::QueryProviderCapabilities capabilities{true, true, false, false};

    const auto facts_result = queries_.RegisterSnapshotProvider<FindFactsQuery, facts::FactsSnapshot>(
        "framework.query.facts.find",
        capabilities,
        [this](const FindFactsQuery& query, const queries::QueryContext&) {
            auto values = facts_.FindFacts(query.type, query.subject, query.scope);
            queries::QueryMetadata metadata;
            metadata.revision = facts_.FactRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindFactsQuery::ResultType>>::Success(
                {std::move(values), metadata});
        },
        [this](const queries::QueryContext&) {
            auto snapshot = facts_.CaptureSnapshot();
            if (!snapshot)
            {
                return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Failure(snapshot.GetError());
            }
            return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Success(
                {snapshot.Value(), snapshot.Value().fact_revision});
        },
        [](const FindFactsQuery& query, const queries::QueryContext&, const facts::FactsSnapshot& snapshot) {
            std::vector<facts::FactRecord> values;
            for (const auto& fact : snapshot.facts)
            {
                if (fact.key.type == query.type && (!query.subject.IsValid() || fact.key.subject == query.subject) &&
                    (!query.scope.IsValid() || fact.key.scope == query.scope))
                {
                    values.push_back(fact);
                }
            }
            std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
            queries::QueryMetadata metadata;
            metadata.revision = snapshot.fact_revision;
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindFactsQuery::ResultType>>::Success(
                {std::move(values), metadata});
        });
    if (!facts_result)
    {
        return facts_result;
    }

    return queries_.RegisterSnapshotProvider<FindHistoryQuery, facts::FactsSnapshot>(
        "framework.query.history.find",
        capabilities,
        [this](const FindHistoryQuery& query, const queries::QueryContext&) {
            auto values = facts_.FindHistory(query.type, query.subject);
            queries::QueryMetadata metadata;
            metadata.revision = facts_.FactRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindHistoryQuery::ResultType>>::Success(
                {std::move(values), metadata});
        },
        [this](const queries::QueryContext&) {
            auto snapshot = facts_.CaptureSnapshot();
            if (!snapshot)
            {
                return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Failure(snapshot.GetError());
            }
            return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Success(
                {snapshot.Value(), snapshot.Value().fact_revision});
        },
        [](const FindHistoryQuery& query, const queries::QueryContext&, const facts::FactsSnapshot& snapshot) {
            std::vector<facts::EventRecord> values;
            for (const auto& event : snapshot.history)
            {
                if (event.envelope.type == query.type && (!query.subject.IsValid() || event.envelope.subject == query.subject))
                {
                    values.push_back(event);
                }
            }
            std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
                return left.envelope.sequence < right.envelope.sequence;
            });
            queries::QueryMetadata metadata;
            metadata.revision = snapshot.fact_revision;
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindHistoryQuery::ResultType>>::Success(
                {std::move(values), metadata});
        });
}

foundation::Result<void> RuntimeTimeAdapter::Synchronize()
{
    const auto snapshot = runtime_clock_.GetSnapshot();
    return gameplay_time_.SynchronizeClock(clock_, GameplayTimePoint{snapshot.now.ticks}, Revision{snapshot.revision});
}

foundation::Result<EventTypeId> TimeFactsAdapter::RegisterContracts()
{
    const auto registered = facts_.RegisterEventType<time::ScheduledTrigger>(
        "framework.schedule.due",
        GameplayDomainId::FromString("framework.time"),
        facts::HistoryPolicy::None);
    if (registered)
    {
        scheduled_due_event_type_ = registered.Value();
    }
    return registered;
}

foundation::Result<std::vector<time::ScheduledTrigger>> TimeFactsAdapter::CollectAndPublish(
    ClockId clock,
    GameplayContext context,
    time::SchedulerBudget budget)
{
    if (!scheduled_due_event_type_.IsValid())
    {
        return foundation::Result<std::vector<time::ScheduledTrigger>>::Failure(
            foundation::Error::Create("gameplay.time_facts_unregistered", "time/facts contracts must be registered before use"));
    }

    auto collected = time_.CollectDue(clock, budget);
    if (!collected)
    {
        return foundation::Result<std::vector<time::ScheduledTrigger>>::Failure(collected.GetError());
    }

    auto triggers = std::move(collected).Value();
    for (const auto& trigger : triggers)
    {
        auto event_context = context;
        event_context.time = trigger.observed_at;
        const auto published = facts_.Publish<time::ScheduledTrigger>(
            scheduled_due_event_type_,
            event_context,
            trigger.owner,
            trigger,
            ProducerId::FromString("framework.time"));
        if (!published)
        {
            return foundation::Result<std::vector<time::ScheduledTrigger>>::Failure(published.GetError());
        }
    }

    return foundation::Result<std::vector<time::ScheduledTrigger>>::Success(std::move(triggers));
}

foundation::Result<std::uint64_t> TimeFactsAdapter::ExpireTimedFacts(GameplayContext context)
{
    return facts_.ExpireDueFacts(context.time, context);
}
} // namespace epidemic::gameplay::integration
