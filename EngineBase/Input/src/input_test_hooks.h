#pragma once

namespace epidemic::input::testing
{
// Test-only deterministic boundary for PublishSnapshot candidate allocation.
void FailNextCandidateEventAllocation() noexcept;
void ClearInputFaults() noexcept;
[[nodiscard]] bool ConsumeCandidateEventAllocationFailure() noexcept;
} // namespace epidemic::input::testing
