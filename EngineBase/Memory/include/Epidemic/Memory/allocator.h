#pragma once

#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/imemory_tracker.h>

#include <cstddef>
#include <new>

namespace epidemic::memory
{
[[nodiscard]] constexpr std::size_t NormalizeAllocationSize(std::size_t bytes) noexcept
{
    return bytes == 0 ? std::size_t{1} : bytes;
}

class IAllocator
{
  public:
    virtual ~IAllocator() = default;

    virtual void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                           AllocationTag tag = AllocationTag::Unknown) = 0;
    virtual void Deallocate(void *pointer, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                            AllocationTag tag = AllocationTag::Unknown) noexcept = 0;
};

class DefaultAllocator final : public IAllocator
{
  public:
    void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                   AllocationTag = AllocationTag::Unknown) override
    {
        return ::operator new(NormalizeAllocationSize(bytes), std::align_val_t(alignment));
    }

    void Deallocate(void *pointer, std::size_t, std::size_t alignment = alignof(std::max_align_t),
                    AllocationTag = AllocationTag::Unknown) noexcept override
    {
        if (pointer == nullptr)
        {
            return;
        }

        ::operator delete(pointer, std::align_val_t(alignment));
    }
};

// TrackingAllocator mirrors the allocation contract of its upstream allocator.
// The caller must pass the same bytes, alignment and tag to Deallocate that were
// used for Allocate; otherwise the recorded statistics become inaccurate.
// Zero-sized requests are normalized to 1 byte for tracking so the counters stay
// consistent with DefaultAllocator and with upstream allocators that materialize
// a minimal allocation for size 0.
class TrackingAllocator final : public IAllocator
{
  public:
    TrackingAllocator(IMemoryTracker &tracker, IAllocator &upstream) noexcept : tracker_(tracker), upstream_(upstream)
    {
    }

    void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                   AllocationTag tag = AllocationTag::Unknown) override
    {
        void *pointer = upstream_.Allocate(bytes, alignment, tag);
        tracker_.RecordAllocate(tag, NormalizeAllocationSize(bytes));
        return pointer;
    }

    void Deallocate(void *pointer, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                    AllocationTag tag = AllocationTag::Unknown) noexcept override
    {
        if (pointer == nullptr)
        {
            return;
        }

        upstream_.Deallocate(pointer, bytes, alignment, tag);
        tracker_.RecordFree(tag, NormalizeAllocationSize(bytes));
    }

  private:
    IMemoryTracker &tracker_;
    IAllocator &upstream_;
};

class IFrameAllocator
{
  public:
    virtual ~IFrameAllocator() = default;

    virtual void Reset() noexcept = 0;
};
} // namespace epidemic::memory