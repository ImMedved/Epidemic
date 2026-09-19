#pragma once

namespace epidemic::platform::testing
{
enum class FaultPoint
{
    EventQueueCommit,
    WindowTrackingCommit,
    AfterWindowIdInsert,
    AfterNativeLibraryLoad,
};

void FailNext(FaultPoint fault_point) noexcept;
void ClearFaults() noexcept;
[[nodiscard]] bool Consume(FaultPoint fault_point) noexcept;
} // namespace epidemic::platform::testing
