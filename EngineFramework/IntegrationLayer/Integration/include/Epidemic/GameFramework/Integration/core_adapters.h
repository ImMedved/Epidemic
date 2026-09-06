#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Facts/gameplay_facts.h"
#include "Epidemic/GameFramework/Queries/gameplay_queries.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"
#include "Epidemic/Runtime/Time/time_runtime.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
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

    // Composition contract: register once before Queries::Freeze(). Repeated calls on the same adapter are idempotent.
    [[nodiscard]] foundation::Result<void> RegisterProviders();
    [[nodiscard]] bool IsRegistered() const noexcept { return registered_; }

  private:
    facts::GameplayFactsService& facts_;
    queries::GameplayQueryService& queries_;
    bool registered_ = false;
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

using ScheduledTriggerHandlerId = TypeId;

enum class ScheduledTriggerDisposition
{
    Ack,
    Retry,
    DiscardTerminal,
};

struct ScheduledTriggerDispatcherPolicy
{
    std::size_t max_pending_deliveries = 16384;
    std::uint64_t max_delivery_attempts_per_pump = 16384;
};

struct ScheduledTriggerDeliveryRecord
{
    time::ScheduledTrigger trigger{};
    GameplayContext context{};
    std::vector<ScheduledTriggerHandlerId> completed_handlers;
};

struct ScheduledTriggerDispatcherSnapshot
{
    std::vector<ScheduledTriggerDeliveryRecord> pending;
};

struct ScheduledTriggerPumpReport
{
    std::uint64_t collected = 0;
    std::uint64_t acknowledged = 0;
    std::uint64_t discarded_terminal = 0;
    std::uint64_t retry_requests = 0;
    std::uint64_t handler_failures = 0;
    std::uint64_t unhandled = 0;
    std::uint64_t pending = 0;
};

class ScheduledTriggerDispatcher
{
  public:
    using Handler = std::function<foundation::Result<ScheduledTriggerDisposition>(const time::ScheduledTrigger&, const GameplayContext&)>;

    ScheduledTriggerDispatcher(
        time::GameplayTimeService& time_service,
        ScheduledTriggerDispatcherPolicy policy = {})
        : time_(time_service), policy_(policy)
    {
    }

    // Observers receive every collected trigger. Action handlers receive only their registered action.
    // Handler registrations are immutable after Freeze(). IDs are stable delivery-leg identities used by snapshots/retries.
    [[nodiscard]] foundation::Result<void> RegisterObserver(ScheduledTriggerHandlerId id, int priority, Handler handler);
    [[nodiscard]] foundation::Result<void> RegisterActionHandler(ActionTypeId action, ScheduledTriggerHandlerId id, Handler handler);
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    // This is the only Integration-level API that destructively collects from GameplayTimeService.
    // Collected triggers are first stored in pending delivery state before any handler is invoked.
    [[nodiscard]] foundation::Result<std::uint64_t> CollectDue(
        ClockId clock,
        GameplayContext context,
        time::SchedulerBudget budget = {});

    [[nodiscard]] ScheduledTriggerPumpReport DispatchPending(
        std::uint64_t max_attempts = 0);

    [[nodiscard]] foundation::Result<ScheduledTriggerPumpReport> Pump(
        ClockId clock,
        GameplayContext context,
        time::SchedulerBudget budget = {});

    [[nodiscard]] ScheduledTriggerDispatcherSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(ScheduledTriggerDispatcherSnapshot snapshot);

    [[nodiscard]] std::size_t PendingCount() const noexcept { return pending_.size(); }

  private:
    struct RegisteredHandler
    {
        ScheduledTriggerHandlerId id{};
        int priority = 0;
        Handler callback;
    };

    [[nodiscard]] bool HandlerExists(ScheduledTriggerHandlerId id) const noexcept;
    [[nodiscard]] const RegisteredHandler* FindHandler(ScheduledTriggerHandlerId id) const noexcept;
    [[nodiscard]] std::vector<ScheduledTriggerHandlerId> RequiredHandlersFor(ActionTypeId action) const;
    [[nodiscard]] foundation::Result<void> ValidateSnapshot(const ScheduledTriggerDispatcherSnapshot& snapshot) const;

    time::GameplayTimeService& time_;
    ScheduledTriggerDispatcherPolicy policy_{};
    std::vector<RegisteredHandler> observers_;
    std::unordered_map<ActionTypeId, RegisteredHandler> action_handlers_;
    std::vector<ScheduledTriggerDeliveryRecord> pending_;
    bool frozen_ = false;
};

class TimeFactsAdapter
{
  public:
    TimeFactsAdapter(facts::GameplayFactsService& facts_service)
        : facts_(facts_service)
    {
    }

    // Register once before Facts::Freeze(). Repeated calls on the same adapter are idempotent.
    [[nodiscard]] foundation::Result<EventTypeId> RegisterContracts();

    // Registers as a dispatcher observer. It no longer calls Time::CollectDue directly.
    [[nodiscard]] foundation::Result<void> RegisterWithDispatcher(
        ScheduledTriggerDispatcher& dispatcher,
        int priority = 0);

    [[nodiscard]] foundation::Result<ScheduledTriggerDisposition> PublishTrigger(
        const time::ScheduledTrigger& trigger,
        const GameplayContext& context);

    [[nodiscard]] foundation::Result<std::uint64_t> ExpireTimedFacts(GameplayContext context);

    [[nodiscard]] EventTypeId ScheduledDueEventType() const noexcept { return scheduled_due_event_type_; }

  private:
    facts::GameplayFactsService& facts_;
    EventTypeId scheduled_due_event_type_{};
    bool dispatcher_registered_ = false;
};
} // namespace epidemic::gameplay::integration
