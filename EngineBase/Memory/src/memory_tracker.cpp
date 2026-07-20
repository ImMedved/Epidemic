#include <Epidemic/Memory/memory_tracker.h>

#include <algorithm>

namespace epidemic::memory
{
// This file implements the default thread-safe tag-based memory accounting service.
// The implementation is intentionally conservative: all state transitions are serialized
// by one mutex, which keeps the baseline simple and predictable.

// Stores the initial tracking-enabled state.
MemoryTracker::MemoryTracker(bool tracking_enabled) noexcept : tracking_enabled_(tracking_enabled)
{
}

// Enables or disables future accounting updates.
void MemoryTracker::SetTrackingEnabled(bool tracking_enabled) noexcept
{
    std::scoped_lock lock(mutex_);
    tracking_enabled_ = tracking_enabled;
}

// Returns whether accounting updates are currently enabled.
bool MemoryTracker::IsTrackingEnabled() const noexcept
{
    std::scoped_lock lock(mutex_);
    return tracking_enabled_;
}

// Records allocated bytes for the supplied tag.
// Input: tag and normalized byte count.
// Behavior: ignored when tracking is disabled or bytes is zero.
void MemoryTracker::RecordAllocate(AllocationTag tag, std::size_t bytes) noexcept
{
    std::scoped_lock lock(mutex_);
    if (!tracking_enabled_ || bytes == 0)
    {
        return;
    }

    auto &statistics = statistics_[ToIndex(tag)];
    statistics.allocated_bytes += bytes;
    statistics.peak_allocated_bytes = std::max(statistics.peak_allocated_bytes, statistics.allocated_bytes);
    ++statistics.allocation_count;
}

// Records freed bytes for the supplied tag.
// Behavior: usage is clamped at zero instead of underflowing.
void MemoryTracker::RecordFree(AllocationTag tag, std::size_t bytes) noexcept
{
    std::scoped_lock lock(mutex_);
    if (!tracking_enabled_ || bytes == 0)
    {
        return;
    }

    auto &statistics = statistics_[ToIndex(tag)];
    statistics.allocated_bytes = bytes >= statistics.allocated_bytes ? 0 : statistics.allocated_bytes - bytes;
}

// Sets a soft budget for a tag.
void MemoryTracker::SetBudget(AllocationTag tag, std::size_t bytes) noexcept
{
    std::scoped_lock lock(mutex_);
    budgets_[ToIndex(tag)] = bytes;
}

// Returns the currently configured budget for a tag.
std::optional<std::size_t> MemoryTracker::GetBudget(AllocationTag tag) const noexcept
{
    std::scoped_lock lock(mutex_);
    return budgets_[ToIndex(tag)];
}

// Returns the current allocated byte count for a tag.
std::size_t MemoryTracker::GetUsage(AllocationTag tag) const noexcept
{
    std::scoped_lock lock(mutex_);
    return statistics_[ToIndex(tag)].allocated_bytes;
}

// Returns true when a configured budget exists and usage exceeds it.
bool MemoryTracker::IsOverBudget(AllocationTag tag) const noexcept
{
    std::scoped_lock lock(mutex_);
    const auto index = ToIndex(tag);
    if (!budgets_[index].has_value())
    {
        return false;
    }

    return statistics_[index].allocated_bytes > *budgets_[index];
}

// Returns the full statistics snapshot for a tag.
MemoryTagStatistics MemoryTracker::GetStatistics(AllocationTag tag) const noexcept
{
    std::scoped_lock lock(mutex_);
    return statistics_[ToIndex(tag)];
}

// Clears all accumulated per-tag statistics.
void MemoryTracker::ResetStatistics() noexcept
{
    std::scoped_lock lock(mutex_);
    statistics_.fill(MemoryTagStatistics{});
}

// Converts external tag input into a normalized array index.
std::size_t MemoryTracker::ToIndex(AllocationTag tag) noexcept
{
    return static_cast<std::size_t>(NormalizeAllocationTag(tag));
}
} // namespace epidemic::memory