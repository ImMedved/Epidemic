#pragma once
#include "Epidemic/GameFramework/Interaction/interaction.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"
#include <span>
namespace epidemic::gameplay::interaction_time_integration
{
class InteractionTimeAdapter
{
public:
    InteractionTimeAdapter(interaction::InteractionService& interactions,time::GameplayTimeService& time):interactions_(interactions),time_(time){}
    [[nodiscard]] foundation::Result<void> RegisterContracts();
    [[nodiscard]] foundation::Result<std::uint64_t> Synchronize(ClockId clock);
    [[nodiscard]] foundation::Result<std::uint64_t> ProcessTriggers(std::span<const time::ScheduledTrigger> triggers,GameplayContext context={});
    [[nodiscard]] ActionTypeId CompleteAction()const noexcept{return complete_action_;}
private:
    [[nodiscard]] static GameplayObjectRef SessionRef(interaction::InteractionExecutionId id) noexcept{return {interaction::InteractionService::Domain(),id.value};}
    interaction::InteractionService&interactions_;time::GameplayTimeService&time_;ActionTypeId complete_action_{};std::uint64_t cursor_=0;
};
}
