#pragma once

#include <Epidemic/Memory/allocation_tag.h>

#include <cstddef>
#include <optional>

namespace epidemic::memory
{
struct MemoryTagStatistics;

class IMemoryTracker
{
  public:
    virtual ~IMemoryTracker() = default;

    virtual void SetTrackingEnabled(bool tracking_enabled) noexcept = 0;
    [[nodiscard]] virtual bool IsTrackingEnabled() const noexcept = 0;

    virtual void RecordAllocate(AllocationTag tag, std::size_t bytes) noexcept = 0;
    virtual void RecordFree(AllocationTag tag, std::size_t bytes) noexcept = 0;

    virtual void SetBudget(AllocationTag tag, std::size_t bytes) noexcept = 0;
    [[nodiscard]] virtual std::optional<std::size_t> GetBudget(AllocationTag tag) const noexcept = 0;
    [[nodiscard]] virtual std::size_t GetUsage(AllocationTag tag) const noexcept = 0;
    [[nodiscard]] virtual bool IsOverBudget(AllocationTag tag) const noexcept = 0;
    [[nodiscard]] virtual MemoryTagStatistics GetStatistics(AllocationTag tag) const noexcept = 0;

    virtual void ResetStatistics() noexcept = 0;
};
} // namespace epidemic::memory