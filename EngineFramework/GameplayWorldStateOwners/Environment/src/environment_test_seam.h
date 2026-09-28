#pragma once

namespace epidemic::gameplay::environment::test_seam
{
enum class FaultPoint
{
    None,
    RestoreCandidateBuild,
    RestoreBeforeCommit,
};

inline thread_local FaultPoint next_fault = FaultPoint::None;

inline void FailNext(FaultPoint point) noexcept
{
    next_fault = point;
}

[[nodiscard]] inline bool Consume(FaultPoint point) noexcept
{
    if (next_fault != point)
        return false;
    next_fault = FaultPoint::None;
    return true;
}

inline void Reset() noexcept
{
    next_fault = FaultPoint::None;
}
} // namespace epidemic::gameplay::environment::test_seam
