#pragma once

#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/imemory_tracker.h>

#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>

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
    // Contract:
    // - zero bytes is a valid request and materializes one byte;
    // - bytes larger than PTRDIFF_MAX are rejected as an invalid object size;
    // - alignment must be a non-zero power of two;
    // - operational allocation failure may propagate std::bad_alloc or return nullptr from
    //   an upstream implementation; a decorator must not publish accounting for either case.
    virtual void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                           AllocationTag tag = AllocationTag::Unknown) = 0;

    // Releases a block previously allocated by Allocate.
    // The pointer, bytes, alignment and tag must match the original allocation. nullptr is a no-op.
    // An invalid representable size/alignment is a controlled no-op so callers can retry with the
    // correct metadata without invoking undefined aligned-delete behavior.
    virtual void Deallocate(void *pointer, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                            AllocationTag tag = AllocationTag::Unknown) noexcept = 0;
};

// Thin allocator that forwards directly to aligned global new/delete.
class DefaultAllocator final : public IAllocator
{
  public:
    // Allocates storage through aligned operator new after validating the common allocator contract.
    void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                   AllocationTag = AllocationTag::Unknown) override
    {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        {
            throw std::invalid_argument("allocation alignment must be a non-zero power of two");
        }
        if (bytes > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        {
            throw std::length_error("allocation size exceeds PTRDIFF_MAX");
        }

        return ::operator new(NormalizeAllocationSize(bytes), std::align_val_t(alignment));
    }

    // Releases storage through aligned operator delete. Null or invalid requests are ignored.
    void Deallocate(void *pointer, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                    AllocationTag = AllocationTag::Unknown) noexcept override
    {
        if (pointer == nullptr || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
            bytes > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
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

    // Allocates through the upstream allocator and records exactly one successful allocation.
    void *Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                   AllocationTag tag = AllocationTag::Unknown) override
    {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        {
            throw std::invalid_argument("allocation alignment must be a non-zero power of two");
        }
        if (bytes > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        {
            throw std::length_error("allocation size exceeds PTRDIFF_MAX");
        }

        void *pointer = upstream_.Allocate(bytes, alignment, tag);
        if (pointer == nullptr)
        {
            return nullptr;
        }

        tracker_.RecordAllocate(tag, NormalizeAllocationSize(bytes));
        return pointer;
    }

    // Frees through the upstream allocator and mirrors a valid non-null free into the tracker.
    void Deallocate(void *pointer, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t),
                    AllocationTag tag = AllocationTag::Unknown) noexcept override
    {
        if (pointer == nullptr || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
            bytes > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
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
// No concrete production frame allocator exists in EngineBase yet. Implementations must make
// Reset the explicit lifetime boundary: storage obtained before Reset becomes invalid only when
// Reset is called by the owning frame/batch coordinator, never implicitly from Allocate/Deallocate.
class IFrameAllocator
{
  public:
    virtual ~IFrameAllocator() = default;

    // Resets transient frame-local allocation state and invalidates the previous frame lifetime.
    virtual void Reset() noexcept = 0;
};
} // namespace epidemic::memory
