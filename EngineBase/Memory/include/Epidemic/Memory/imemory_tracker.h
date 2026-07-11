#pragma once

#include <Epidemic/Memory/allocation_tag.h>

#include <cstddef>
#include <optional>

namespace epidemic::memory
{
// This file defines the public memory tracking interface consumed by EngineBase services.
// The interface abstracts accounting so allocators and support code can depend on behavior
// rather than on MemoryTracker's concrete implementation.

struct MemoryTagStatistics
{
    std::size_t allocated_bytes{};
    std::size_t peak_allocated_bytes{};
    // Counts successful allocation records over time, not the number of active allocations.
    std::size_t allocation_count{};
};

// Public memory accounting interface.
class IMemoryTracker
{
  public:
    virtual ~IMemoryTracker() = default;

    // Enables or disables accounting updates.
    virtual void SetTrackingEnabled(bool tracking_enabled) noexcept = 0;

    // Returns whether accounting updates are currently enabled.
    [[nodiscard]] virtual bool IsTrackingEnabled() const noexcept = 0;

    // Records a successful allocation event under the supplied tag.
    virtual void RecordAllocate(AllocationTag tag, std::size_t bytes) noexcept = 0;

    // Records a successful free event under the supplied tag.
    virtual void RecordFree(AllocationTag tag, std::size_t bytes) noexcept = 0;

    // Sets a soft budget for the supplied tag.
    virtual void SetBudget(AllocationTag tag, std::size_t bytes) noexcept = 0;

    // Returns the configured budget for the tag, if any.
    [[nodiscard]] virtual std::optional<std::size_t> GetBudget(AllocationTag tag) const noexcept = 0;

    // Returns current allocated bytes for the tag.
    [[nodiscard]] virtual std::size_t GetUsage(AllocationTag tag) const noexcept = 0;

    // Returns true when current usage exceeds the configured budget.
    [[nodiscard]] virtual bool IsOverBudget(AllocationTag tag) const noexcept = 0;

    // Returns the full statistics snapshot for the tag.
    [[nodiscard]] virtual MemoryTagStatistics GetStatistics(AllocationTag tag) const noexcept = 0;

    // Clears accumulated statistics for all tags.
    virtual void ResetStatistics() noexcept = 0;
};
} // namespace epidemic::memory