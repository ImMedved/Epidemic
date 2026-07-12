#pragma once


// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.
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
