#pragma once

namespace epidemic::gameplay::ai::internal_test
{
enum class AllocationFaultPoint
{
    None, RegisterProfilePublication, RegisterAgentPrimary, RegisterAgentSchedule, ScheduleThinkIndex,
    ThinkDecisionStaging, ThinkIntentIndex, ThinkDueIndex
};
inline thread_local AllocationFaultPoint g_allocation_fault = AllocationFaultPoint::None;
inline void ArmAllocationFault(AllocationFaultPoint point) noexcept { g_allocation_fault = point; }
inline void ResetAllocationFault() noexcept { g_allocation_fault = AllocationFaultPoint::None; }
[[nodiscard]] inline bool ConsumeAllocationFault(AllocationFaultPoint point) noexcept
{
    if (g_allocation_fault != point) return false;
    g_allocation_fault = AllocationFaultPoint::None;
    return true;
}
} // namespace epidemic::gameplay::ai::internal_test
