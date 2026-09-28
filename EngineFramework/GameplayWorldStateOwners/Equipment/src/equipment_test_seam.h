#pragma once

namespace epidemic::gameplay::equipment::internal_test
{
enum class AllocationFaultPoint
{
    None,
    RestoreProfiles,
    RestoreBindings,
    RestoreLoadouts
};

inline thread_local AllocationFaultPoint g_allocation_fault = AllocationFaultPoint::None;

inline void ArmAllocationFault(AllocationFaultPoint point) noexcept
{
    g_allocation_fault = point;
}

inline void ResetAllocationFault() noexcept
{
    g_allocation_fault = AllocationFaultPoint::None;
}

[[nodiscard]] inline bool ConsumeAllocationFault(AllocationFaultPoint point) noexcept
{
    if (g_allocation_fault != point)
        return false;
    g_allocation_fault = AllocationFaultPoint::None;
    return true;
}
} // namespace epidemic::gameplay::equipment::internal_test
