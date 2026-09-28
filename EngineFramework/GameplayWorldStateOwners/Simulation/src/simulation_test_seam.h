#pragma once

namespace epidemic::gameplay::simulation::internal_test
{
enum class AllocationFaultPoint
{
    None, IntervalPublication, SummaryPublication, JournalPublication, RestoreRegions, RestoreSummaries, RestoreIntervals
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
} // namespace epidemic::gameplay::simulation::internal_test
