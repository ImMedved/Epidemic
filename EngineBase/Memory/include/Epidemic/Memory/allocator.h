#pragma once

#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/imemory_tracker.h>

#include <cstddef>
#include <new>

namespace epidemic::memory
{
// This file contains the minimal allocator contracts used by the memory baseline.
// The allocators are intentionally simple: they define allocation/deallocation flow and
// optional tracking, but they do not implement arenas, pooling, streaming, or asset systems.

// Normalizes zero-byte allocation requests to a single tracked byte.
// Relationship: keeps tracking behavior consistent with DefaultAllocator materializing storage.
[[nodiscard]] constexpr std::size_t NormalizeAllocationSize(std::size_t bytes) noexcept
{
    return bytes == 0 ? std::size_t{1} : bytes;
}

// Minimal allocation interface used by the EngineBase memory baseline.
class IAllocator
{
  public:
    virtual ~IAllocator() = default;

    // Allocates a block of memory.
    // Inputs:
    // - bytes: requested size in bytes
    // - alignment: requested alignment
    // - tag: accounting category for diagnostics/tracking
    // Output: pointer to allocated storage, or exception from the concrete allocator on failure.
    virtual void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                           AllocationTag tag = AllocationTag::Unknown) = 0;

    // Releases a block previously allocated by Allocate.
    // Inputs must match the original allocation contract for allocators/tracking to remain correct.
    virtual void Deallocate(void *pointer, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                            AllocationTag tag = AllocationTag::Unknown) noexcept = 0;
};

// Thin allocator that forwards directly to aligned global new/delete.
class DefaultAllocator final : public IAllocator
{
  public:
    // Allocates storage through aligned operator new.
    void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                   AllocationTag = AllocationTag::Unknown) override
    {
        return ::operator new(NormalizeAllocationSize(bytes), std::align_val_t(alignment));
    }

    // Releases storage through aligned operator delete.
    // Null pointers are ignored.
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
    // Binds a tracker interface to an upstream allocator implementation.
    TrackingAllocator(IMemoryTracker &tracker, IAllocator &upstream) noexcept : tracker_(tracker), upstream_(upstream)
    {
    }

    // Allocates through the upstream allocator and records the normalized size.
    void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                   AllocationTag tag = AllocationTag::Unknown) override
    {
        void *pointer = upstream_.Allocate(bytes, alignment, tag);
        tracker_.RecordAllocate(tag, NormalizeAllocationSize(bytes));
        return pointer;
    }

    // Frees through the upstream allocator and mirrors the free into the tracker.
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

// Minimal per-frame allocator hook.
// Relationship: future frame allocators can implement this without changing higher APIs.
class IFrameAllocator
{
  public:
    virtual ~IFrameAllocator() = default;

    // Resets transient frame-local allocation state.
    virtual void Reset() noexcept = 0;
};
} // namespace epidemic::memory