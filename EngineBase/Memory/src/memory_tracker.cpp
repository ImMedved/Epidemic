#include <Epidemic/Memory/memory_tracker.h>

#include <algorithm>

namespace epidemic::memory
{
MemoryTracker::MemoryTracker(bool tracking_enabled) noexcept : tracking_enabled_(tracking_enabled)
{
}

void MemoryTracker::SetTrackingEnabled(bool tracking_enabled) noexcept
{
    std::scoped_lock lock(mutex_);
    tracking_enabled_ = tracking_enabled;
}

bool MemoryTracker::IsTrackingEnabled() const noexcept
{
    std::scoped_lock lock(mutex_);
    return tracking_enabled_;
}

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

void MemoryTracker::SetBudget(AllocationTag tag, std::size_t bytes) noexcept
{
    std::scoped_lock lock(mutex_);
    budgets_[ToIndex(tag)] = bytes;
}

std::optional<std::size_t> MemoryTracker::GetBudget(AllocationTag tag) const noexcept
{
    std::scoped_lock lock(mutex_);
    return budgets_[ToIndex(tag)];
}

std::size_t MemoryTracker::GetUsage(AllocationTag tag) const noexcept
{
    std::scoped_lock lock(mutex_);
    return statistics_[ToIndex(tag)].allocated_bytes;
}

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

MemoryTagStatistics MemoryTracker::GetStatistics(AllocationTag tag) const noexcept
{
    std::scoped_lock lock(mutex_);
    return statistics_[ToIndex(tag)];
}

void MemoryTracker::ResetStatistics() noexcept
{
    std::scoped_lock lock(mutex_);
    statistics_.fill(MemoryTagStatistics{});
}

std::size_t MemoryTracker::ToIndex(AllocationTag tag) noexcept
{
    return static_cast<std::size_t>(NormalizeAllocationTag(tag));
}
} // namespace epidemic::memory