#pragma once

#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/imemory_tracker.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>

namespace epidemic::memory
{
// This file declares the default IMemoryTracker implementation used by EngineBase.
// MemoryTracker provides thread-safe accounting by allocation tag and is intentionally small:
// it tracks usage, peaks, counts, and optional budgets, but does not own allocators.

class MemoryTracker final : public IMemoryTracker
{
  public:
    // Builds the tracker with tracking enabled or disabled from the start.
    explicit MemoryTracker(bool tracking_enabled = true) noexcept;

    // Enables or disables subsequent accounting updates.
    void SetTrackingEnabled(bool tracking_enabled) noexcept override;

    // Returns whether accounting updates are currently enabled.
    [[nodiscard]] bool IsTrackingEnabled() const noexcept override;

    // Records allocated bytes under the supplied tag.
    void RecordAllocate(AllocationTag tag, std::size_t bytes) noexcept override;

    // Records freed bytes under the supplied tag.
    void RecordFree(AllocationTag tag, std::size_t bytes) noexcept override;

    // Sets a budget for the supplied tag.
    void SetBudget(AllocationTag tag, std::size_t bytes) noexcept override;

    // Returns the configured budget for the tag, if any.
    [[nodiscard]] std::optional<std::size_t> GetBudget(AllocationTag tag) const noexcept override;

    // Returns current allocated bytes for the tag.
    [[nodiscard]] std::size_t GetUsage(AllocationTag tag) const noexcept override;

    // Returns true when current usage exceeds the configured budget.
    [[nodiscard]] bool IsOverBudget(AllocationTag tag) const noexcept override;

    // Returns the accumulated statistics for the tag.
    [[nodiscard]] MemoryTagStatistics GetStatistics(AllocationTag tag) const noexcept override;

    // Clears all accumulated statistics.
    void ResetStatistics() noexcept override;

  private:
    // Maps external tag input to a safe array index.
    [[nodiscard]] static std::size_t ToIndex(AllocationTag tag) noexcept;

    mutable std::mutex mutex_;
    bool tracking_enabled_{true};
    std::array<MemoryTagStatistics, AllocationTagCount()> statistics_{};
    std::array<std::optional<std::size_t>, AllocationTagCount()> budgets_{};
};
} // namespace epidemic::memory