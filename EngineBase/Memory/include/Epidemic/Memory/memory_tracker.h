#pragma once

#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/imemory_tracker.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>

namespace epidemic::memory
{
class MemoryTracker final : public IMemoryTracker
{
  public:
    explicit MemoryTracker(bool tracking_enabled = true) noexcept;

    void SetTrackingEnabled(bool tracking_enabled) noexcept override;
    [[nodiscard]] bool IsTrackingEnabled() const noexcept override;

    void RecordAllocate(AllocationTag tag, std::size_t bytes) noexcept override;
    void RecordFree(AllocationTag tag, std::size_t bytes) noexcept override;

    void SetBudget(AllocationTag tag, std::size_t bytes) noexcept override;
    [[nodiscard]] std::optional<std::size_t> GetBudget(AllocationTag tag) const noexcept override;
    [[nodiscard]] std::size_t GetUsage(AllocationTag tag) const noexcept override;
    [[nodiscard]] bool IsOverBudget(AllocationTag tag) const noexcept override;
    [[nodiscard]] MemoryTagStatistics GetStatistics(AllocationTag tag) const noexcept override;

    void ResetStatistics() noexcept override;

  private:
    [[nodiscard]] static std::size_t ToIndex(AllocationTag tag) noexcept;

    mutable std::mutex mutex_;
    bool tracking_enabled_{true};
    std::array<MemoryTagStatistics, AllocationTagCount()> statistics_{};
    std::array<std::optional<std::size_t>, AllocationTagCount()> budgets_{};
};
} // namespace epidemic::memory