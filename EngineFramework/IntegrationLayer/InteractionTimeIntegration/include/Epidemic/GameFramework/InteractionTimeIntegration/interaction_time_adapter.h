#pragma once

#include "Epidemic/GameFramework/Integration/core_adapters.h"
#include "Epidemic/GameFramework/Interaction/interaction.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"

#include <cstdint>
#include <vector>

namespace epidemic::gameplay::interaction_time_integration
{
enum class InteractionTimeReconciliationKind
{
    OrphanScheduleCancellation,
};

struct InteractionTimeReconciliationRecord
{
    InteractionTimeReconciliationKind kind = InteractionTimeReconciliationKind::OrphanScheduleCancellation;
    interaction::InteractionExecutionId execution{};
    ScheduleId schedule{};
};

class InteractionTimeAdapter
{
  public:
    InteractionTimeAdapter(interaction::InteractionService& interactions, time::GameplayTimeService& time)
        : interactions_(interactions), time_(time)
    {
    }

    // Bootstrap contract. Register the Time action and bind this adapter to the shared scheduled-trigger dispatcher
    // before freezing Time/dispatcher registries. Repeated calls on the same adapter are idempotent.
    [[nodiscard]] foundation::Result<void> RegisterContracts();
    [[nodiscard]] foundation::Result<void> RegisterWithDispatcher(integration::ScheduledTriggerDispatcher& dispatcher);

    // Scheduler bindings are a derived projection of active Interaction sessions. Synchronize is safe to call after
    // restore and after an Interaction journal gap; correctness never depends on retaining cursor history.
    [[nodiscard]] foundation::Result<std::uint64_t> Synchronize(ClockId clock);

    // Public primarily for focused tests/alternative dispatcher composition. Normal production delivery comes through
    // RegisterWithDispatcher(). The shared dispatcher owns destructive Time::CollectDue().
    [[nodiscard]] foundation::Result<integration::ScheduledTriggerDisposition> HandleTrigger(
        const time::ScheduledTrigger& trigger,
        GameplayContext context = {});

    [[nodiscard]] ActionTypeId CompleteAction() const noexcept { return complete_action_; }
    [[nodiscard]] std::uint64_t Cursor() const noexcept { return cursor_; }
    [[nodiscard]] const std::vector<InteractionTimeReconciliationRecord>& PendingReconciliations() const noexcept
    {
        return reconciliations_;
    }

  private:
    [[nodiscard]] static GameplayObjectRef SessionRef(interaction::InteractionExecutionId id) noexcept
    {
        return {interaction::InteractionService::Domain(), id.value};
    }

    [[nodiscard]] foundation::Result<std::uint64_t> ReconcileSchedules(ClockId clock);
    [[nodiscard]] foundation::Result<void> CancelOrRecord(
        interaction::InteractionExecutionId execution,
        ScheduleId schedule);
    [[nodiscard]] std::uint64_t LatestRetainedSequence() const;
    [[nodiscard]] bool HasPendingCompletion(interaction::InteractionExecutionId execution) const;

    interaction::InteractionService& interactions_;
    time::GameplayTimeService& time_;
    integration::ScheduledTriggerDispatcher* dispatcher_ = nullptr;
    ActionTypeId complete_action_{};
    std::uint64_t cursor_ = 0;
    std::vector<InteractionTimeReconciliationRecord> reconciliations_;
};
} // namespace epidemic::gameplay::interaction_time_integration
