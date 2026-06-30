#pragma once

#include <Epidemic/Memory/allocation_tag.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>

namespace epidemic::memory
{
struct MemoryTagStatistics
{
    std::size_t allocated_bytes{};
    std::size_t peak_allocated_bytes{};
    // Counts successful allocation records over time, not the number of active allocations.
    std::size_t allocation_count{};
};

class MemoryTracker
{
  public:
    explicit MemoryTracker(bool tracking_enabled = true) noexcept;

    void SetTrackingEnabled(bool tracking_enabled) noexcept;
    [[nodiscard]] bool IsTrackingEnabled() const noexcept;

    void RecordAllocate(AllocationTag tag, std::size_t bytes) noexcept;
    void RecordFree(AllocationTag tag, std::size_t bytes) noexcept;

    void SetBudget(AllocationTag tag, std::size_t bytes) noexcept;
    [[nodiscard]] std::optional<std::size_t> GetBudget(AllocationTag tag) const noexcept;
    [[nodiscard]] std::size_t GetUsage(AllocationTag tag) const noexcept;
    [[nodiscard]] bool IsOverBudget(AllocationTag tag) const noexcept;
    [[nodiscard]] MemoryTagStatistics GetStatistics(AllocationTag tag) const noexcept;

    void ResetStatistics() noexcept;

  private:
    [[nodiscard]] static std::size_t ToIndex(AllocationTag tag) noexcept;

    mutable std::mutex mutex_;
    bool tracking_enabled_{true};
    std::array<MemoryTagStatistics, AllocationTagCount()> statistics_{};
    std::array<std::optional<std::size_t>, AllocationTagCount()> budgets_{};
};
} // namespace epidemic::memory
