#pragma once

#if defined(EPIDEMIC_CORE_ENABLE_TEST_HOOKS)

namespace epidemic::core::tasks::testing
{
enum class FaultPoint
{
    None,
    AfterWorkerStart,
    BeforeQueuePush,
};

void SetFaultPoint(FaultPoint fault_point) noexcept;
void ClearFaultPoint() noexcept;
} // namespace epidemic::core::tasks::testing

#endif
