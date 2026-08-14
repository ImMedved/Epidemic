#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Facts/gameplay_facts.h"
#include "Epidemic/GameFramework/Queries/gameplay_queries.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"
#include "Epidemic/Runtime/Time/time_runtime.h"

#include <vector>

namespace epidemic::gameplay::integration
{
struct FindFactsQuery
{
    using ResultType = std::vector<facts::FactRecord>;
    FactTypeId type{};
    GameplayObjectRef subject{};
    GameplayObjectRef scope{};

    [[nodiscard]] static constexpr QueryTypeId Type() noexcept
    {
        return QueryTypeId::FromString("framework.query.facts.find");
    }
};

struct FindHistoryQuery
{
    using ResultType = std::vector<facts::EventRecord>;
    EventTypeId type{};
    GameplayObjectRef subject{};

    [[nodiscard]] static constexpr QueryTypeId Type() noexcept
    {
        return QueryTypeId::FromString("framework.query.history.find");
    }
};

class FactsQueryAdapter
{
  public:
    FactsQueryAdapter(facts::GameplayFactsService& facts_service, queries::GameplayQueryService& query_service)
        : facts_(facts_service), queries_(query_service)
    {
    }

    [[nodiscard]] foundation::Result<void> RegisterProviders();

  private:
    facts::GameplayFactsService& facts_;
    queries::GameplayQueryService& queries_;
};

class RuntimeTimeAdapter
{
  public:
    RuntimeTimeAdapter(runtime::IGameClock& runtime_clock, time::GameplayTimeService& gameplay_time, ClockId clock)
        : runtime_clock_(runtime_clock), gameplay_time_(gameplay_time), clock_(clock)
    {
    }

    [[nodiscard]] foundation::Result<void> Synchronize();

  private:
    runtime::IGameClock& runtime_clock_;
    time::GameplayTimeService& gameplay_time_;
    ClockId clock_{};
};

class TimeFactsAdapter
{
  public:
    TimeFactsAdapter(time::GameplayTimeService& time_service, facts::GameplayFactsService& facts_service)
        : time_(time_service), facts_(facts_service)
    {
    }

    [[nodiscard]] foundation::Result<EventTypeId> RegisterContracts();
    [[nodiscard]] foundation::Result<std::vector<time::ScheduledTrigger>> CollectAndPublish(
        ClockId clock,
        GameplayContext context,
        time::SchedulerBudget budget = {});
    [[nodiscard]] foundation::Result<std::uint64_t> ExpireTimedFacts(GameplayContext context);

    [[nodiscard]] EventTypeId ScheduledDueEventType() const noexcept { return scheduled_due_event_type_; }

  private:
    time::GameplayTimeService& time_;
    facts::GameplayFactsService& facts_;
    EventTypeId scheduled_due_event_type_{};
};
} // namespace epidemic::gameplay::integration
