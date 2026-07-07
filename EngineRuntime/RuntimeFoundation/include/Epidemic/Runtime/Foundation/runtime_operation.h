#pragma once

namespace epidemic::runtime
{
enum class AsyncOperationStatus
{
    Pending,
    Running,
    WaitingForMainThread,
    PartiallyComplete,
    Completed,
    Failed,
    Cancelled,
};

[[nodiscard]] constexpr bool IsTerminalOperationStatus(AsyncOperationStatus status) noexcept
{
    return status == AsyncOperationStatus::Completed || status == AsyncOperationStatus::Failed ||
           status == AsyncOperationStatus::Cancelled;
}

[[nodiscard]] constexpr bool IsActiveOperationStatus(AsyncOperationStatus status) noexcept
{
    return !IsTerminalOperationStatus(status);
}
} // namespace epidemic::runtime
