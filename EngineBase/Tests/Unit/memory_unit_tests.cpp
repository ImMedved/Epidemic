// This file exercises the complete EngineBase/Memory LOCAL_READY contract.

#include "../test_assert.h"

#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/allocator.h>
#include <Epidemic/Memory/imemory_tracker.h>
#include <Epidemic/Memory/memory_tracker.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

namespace
{
using epidemic::tests::Assert;
using epidemic::memory::AllocationTag;
using epidemic::memory::DefaultAllocator;
using epidemic::memory::IAllocator;
using epidemic::memory::IFrameAllocator;
using epidemic::memory::MemoryTracker;
using epidemic::memory::TrackingAllocator;

class ProbeAllocator final : public IAllocator
{
  public:
    void *Allocate(std::size_t bytes, std::size_t alignment, AllocationTag tag) override
    {
        ++allocate_calls;
        last_bytes = bytes;
        last_alignment = alignment;
        last_tag = tag;
        if (throw_on_allocate)
        {
            throw std::bad_alloc{};
        }
        if (return_null)
        {
            return nullptr;
        }
        return storage.data();
    }

    void Deallocate(void *pointer, std::size_t bytes, std::size_t alignment, AllocationTag tag) noexcept override
    {
        ++deallocate_calls;
        last_pointer = pointer;
        last_bytes = bytes;
        last_alignment = alignment;
        last_tag = tag;
    }

    alignas(64) inline static std::array<std::byte, 256> storage{};
    bool throw_on_allocate{};
    bool return_null{};
    std::size_t allocate_calls{};
    std::size_t deallocate_calls{};
    std::size_t last_bytes{};
    std::size_t last_alignment{};
    AllocationTag last_tag{AllocationTag::Unknown};
    void *last_pointer{};
};

class ProbeFrameAllocator final : public IFrameAllocator
{
  public:
    void Reset() noexcept override
    {
        ++generation;
    }

    std::size_t generation{};
};

void AssertThrowsInvalidArgument(IAllocator &allocator, std::size_t bytes, std::size_t alignment)
{
    bool threw = false;
    try
    {
        static_cast<void>(allocator.Allocate(bytes, alignment, AllocationTag::Tests));
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    Assert(threw, "Invalid alignment must be rejected with std::invalid_argument");
}

void AssertThrowsLengthError(IAllocator &allocator, std::size_t bytes)
{
    bool threw = false;
    try
    {
        static_cast<void>(allocator.Allocate(bytes, alignof(std::max_align_t), AllocationTag::Tests));
    }
    catch (const std::length_error &)
    {
        threw = true;
    }
    Assert(threw, "Unrepresentable object size must be rejected with std::length_error");
}

void TestAllocationContractAndAlignment()
{
    DefaultAllocator allocator;

    void *pointer = allocator.Allocate(64, 64, AllocationTag::Core);
    Assert(pointer != nullptr, "DefaultAllocator must return storage for a normal allocation");
    Assert(reinterpret_cast<std::uintptr_t>(pointer) % 64 == 0, "DefaultAllocator must honor requested alignment");
    allocator.Deallocate(pointer, 64, 64, AllocationTag::Core);

    void *zero_pointer = allocator.Allocate(0, alignof(std::max_align_t), AllocationTag::Core);
    Assert(zero_pointer != nullptr, "Zero-byte allocation must materialize normalized storage");
    Assert(epidemic::memory::NormalizeAllocationSize(0) == 1, "Zero-byte allocation must normalize to one byte");
    Assert(epidemic::memory::NormalizeAllocationSize(17) == 17, "Non-zero allocation size must stay unchanged");
    allocator.Deallocate(zero_pointer, 0, alignof(std::max_align_t), AllocationTag::Core);
}

void TestInvalidAllocationArguments()
{
    DefaultAllocator default_allocator;
    AssertThrowsInvalidArgument(default_allocator, 16, 0);
    AssertThrowsInvalidArgument(default_allocator, 16, 3);
    AssertThrowsLengthError(default_allocator, std::numeric_limits<std::size_t>::max());

    void *pointer = default_allocator.Allocate(16, alignof(std::max_align_t), AllocationTag::Core);
    default_allocator.Deallocate(pointer, 16, 3, AllocationTag::Core);
    default_allocator.Deallocate(pointer, std::numeric_limits<std::size_t>::max(), alignof(std::max_align_t),
                                 AllocationTag::Core);
    default_allocator.Deallocate(pointer, 16, alignof(std::max_align_t), AllocationTag::Core);
    default_allocator.Deallocate(nullptr, 16, 3, AllocationTag::Core);

    MemoryTracker tracker;
    ProbeAllocator upstream;
    TrackingAllocator tracking_allocator(tracker, upstream);
    AssertThrowsInvalidArgument(tracking_allocator, 16, 3);
    AssertThrowsLengthError(tracking_allocator, std::numeric_limits<std::size_t>::max());
    Assert(upstream.allocate_calls == 0, "TrackingAllocator must reject invalid input before touching upstream");
    Assert(tracker.GetUsage(AllocationTag::Tests) == 0, "Rejected allocation must not change tracker usage");
}

void TestTrackingAllocatorExactlyOnce()
{
    MemoryTracker tracker;
    ProbeAllocator upstream;
    TrackingAllocator allocator(tracker, upstream);

    void *pointer = allocator.Allocate(40, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(pointer != nullptr, "TrackingAllocator must return the upstream pointer");
    Assert(upstream.allocate_calls == 1, "TrackingAllocator must call upstream exactly once");
    auto stats = tracker.GetStatistics(AllocationTag::Tests);
    Assert(stats.allocated_bytes == 40, "Successful allocation must be recorded exactly once");
    Assert(stats.allocation_count == 1, "Successful allocation count must increment exactly once");

    allocator.Deallocate(pointer, 40, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(upstream.deallocate_calls == 1, "TrackingAllocator must deallocate upstream exactly once");
    Assert(tracker.GetUsage(AllocationTag::Tests) == 0, "Successful deallocation must return usage to zero");

    allocator.Deallocate(nullptr, 40, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(upstream.deallocate_calls == 1, "Null deallocation must be a no-op for upstream");
    Assert(tracker.GetUsage(AllocationTag::Tests) == 0, "Null deallocation must be a no-op for accounting");
}

void TestAllocationFailureAtomicity()
{
    MemoryTracker tracker;
    ProbeAllocator upstream;
    TrackingAllocator allocator(tracker, upstream);

    upstream.throw_on_allocate = true;
    bool threw_bad_alloc = false;
    try
    {
        static_cast<void>(allocator.Allocate(48, alignof(std::max_align_t), AllocationTag::Core));
    }
    catch (const std::bad_alloc &)
    {
        threw_bad_alloc = true;
    }
    Assert(threw_bad_alloc, "TrackingAllocator must preserve upstream OOM failure");
    Assert(tracker.GetStatistics(AllocationTag::Core).allocated_bytes == 0,
           "Throwing allocation failure must not increase current usage");
    Assert(tracker.GetStatistics(AllocationTag::Core).allocation_count == 0,
           "Throwing allocation failure must not increment allocation count");

    upstream.throw_on_allocate = false;
    upstream.return_null = true;
    void *pointer = allocator.Allocate(48, alignof(std::max_align_t), AllocationTag::Core);
    Assert(pointer == nullptr, "TrackingAllocator must preserve an upstream nullptr failure result");
    Assert(tracker.GetStatistics(AllocationTag::Core).allocated_bytes == 0,
           "Null allocation failure must not increase current usage");
    Assert(tracker.GetStatistics(AllocationTag::Core).allocation_count == 0,
           "Null allocation failure must not increment allocation count");
}

void TestBudgetsPeakAndReset()
{
    MemoryTracker tracker;
    Assert(tracker.GetUsage(AllocationTag::Core) == 0, "Fresh tracker must start with empty usage");
    Assert(!tracker.GetBudget(AllocationTag::Core).has_value(), "Fresh tracker must start without a budget");
    tracker.SetBudget(AllocationTag::Core, 10);
    Assert(tracker.GetBudget(AllocationTag::Core).value_or(0) == 10, "Configured budget must be queryable");

    tracker.RecordAllocate(AllocationTag::Core, 9);
    Assert(!tracker.IsOverBudget(AllocationTag::Core), "Usage below budget must not be over budget");
    tracker.RecordAllocate(AllocationTag::Core, 1);
    Assert(!tracker.IsOverBudget(AllocationTag::Core), "Usage exactly at budget must not be over budget");
    tracker.RecordAllocate(AllocationTag::Core, 1);
    Assert(tracker.IsOverBudget(AllocationTag::Core), "Usage above budget must be over budget");

    tracker.RecordFree(AllocationTag::Core, 6);
    auto stats = tracker.GetStatistics(AllocationTag::Core);
    Assert(stats.allocated_bytes == 5, "Free must subtract from current usage");
    Assert(stats.peak_allocated_bytes == 11, "Free must not reduce historical peak usage");

    tracker.ResetStatistics();
    stats = tracker.GetStatistics(AllocationTag::Core);
    Assert(stats.allocated_bytes == 5, "Statistics reset must preserve live tracked usage");
    Assert(stats.peak_allocated_bytes == 5, "Statistics reset must restart peak from current live usage");
    Assert(stats.allocation_count == 0, "Statistics reset must clear historical allocation count");
    Assert(tracker.GetBudget(AllocationTag::Core).value_or(0) == 10, "Statistics reset must preserve configured budgets");

    tracker.RecordFree(AllocationTag::Core, 5);
    Assert(tracker.GetUsage(AllocationTag::Core) == 0, "Final free must return current usage to zero after reset");
    tracker.RecordFree(AllocationTag::Core, 1);
    Assert(tracker.GetUsage(AllocationTag::Core) == 0, "Over-free accounting must clamp at zero without underflow");
}

void TestAllocationTagIsolationAndInvalidTag()
{
    MemoryTracker tracker;
    tracker.RecordAllocate(AllocationTag::Core, 8);
    tracker.RecordAllocate(AllocationTag::Platform, 13);
    tracker.RecordAllocate(AllocationTag::Tests, 21);

    Assert(tracker.GetUsage(AllocationTag::Core) == 8, "Core accounting must remain isolated by tag");
    Assert(tracker.GetUsage(AllocationTag::Platform) == 13, "Platform accounting must remain isolated by tag");
    Assert(tracker.GetUsage(AllocationTag::Tests) == 21, "Tests accounting must remain isolated by tag");

    const auto invalid_tag = static_cast<AllocationTag>(255);
    Assert(!epidemic::memory::IsKnownAllocationTag(invalid_tag), "Out-of-range allocation tag must be unknown");
    Assert(epidemic::memory::NormalizeAllocationTag(invalid_tag) == AllocationTag::Unknown,
           "Out-of-range allocation tag must normalize to Unknown");
    tracker.RecordAllocate(invalid_tag, 5);
    Assert(tracker.GetUsage(AllocationTag::Unknown) == 5, "Invalid tag accounting must be isolated in Unknown");
    Assert(epidemic::memory::ToString(invalid_tag) == "Unknown", "Invalid allocation tag must stringify as Unknown");
    Assert(epidemic::memory::AllocationTagCount() == static_cast<std::size_t>(AllocationTag::Count),
           "AllocationTagCount must match the valid accounting range");
}

void TestCounterOverflowDoesNotWrap()
{
    MemoryTracker tracker;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();

    tracker.RecordAllocate(AllocationTag::Diagnostics, maximum - 4);
    tracker.RecordAllocate(AllocationTag::Diagnostics, 10);
    const auto stats = tracker.GetStatistics(AllocationTag::Diagnostics);
    Assert(stats.allocated_bytes == maximum, "Allocated byte counter must saturate instead of wrapping");
    Assert(stats.peak_allocated_bytes == maximum, "Peak byte counter must saturate instead of wrapping");
    Assert(stats.allocation_count == 2, "Normal allocation-count increments must remain exact before saturation");

    tracker.SetBudget(AllocationTag::Diagnostics, maximum - 1);
    Assert(tracker.IsOverBudget(AllocationTag::Diagnostics), "Saturated usage must compare correctly to lower budgets");
    tracker.SetBudget(AllocationTag::Diagnostics, maximum);
    Assert(!tracker.IsOverBudget(AllocationTag::Diagnostics), "Usage at maximum budget must not wrap or report over-budget");

    tracker.RecordFree(AllocationTag::Diagnostics, maximum);
    Assert(tracker.GetUsage(AllocationTag::Diagnostics) == 0, "Free from saturated usage must not underflow");
}

void TestTrackingEnableDisableAndFrameResetContract()
{
    MemoryTracker tracker(false);
    Assert(!tracker.IsTrackingEnabled(), "Tracker must honor disabled construction state");
    tracker.RecordAllocate(AllocationTag::Tests, 32);
    Assert(tracker.GetUsage(AllocationTag::Tests) == 0, "Disabled tracker must ignore allocation records");
    tracker.SetTrackingEnabled(true);
    Assert(tracker.IsTrackingEnabled(), "Tracker must become enabled explicitly");
    tracker.RecordAllocate(AllocationTag::Tests, 32);
    Assert(tracker.GetUsage(AllocationTag::Tests) == 32, "Enabled tracker must record allocations");
    tracker.SetTrackingEnabled(false);
    tracker.RecordFree(AllocationTag::Tests, 32);
    Assert(tracker.GetUsage(AllocationTag::Tests) == 32, "Disabled tracker must ignore free records consistently");
    tracker.SetTrackingEnabled(true);
    tracker.RecordFree(AllocationTag::Tests, 32);
    Assert(tracker.GetUsage(AllocationTag::Tests) == 0, "Explicitly re-enabled tracker must resume accounting");

    ProbeFrameAllocator frame_allocator;
    Assert(frame_allocator.generation == 0, "Frame lifetime must not reset implicitly");
    frame_allocator.Reset();
    Assert(frame_allocator.generation == 1, "IFrameAllocator lifetime boundary must occur only on explicit Reset");
}
} // namespace

int main()
{
    return epidemic::tests::RunNamedTests({
        {"AllocationContractAndAlignment", &TestAllocationContractAndAlignment},
        {"InvalidAllocationArguments", &TestInvalidAllocationArguments},
        {"TrackingAllocatorExactlyOnce", &TestTrackingAllocatorExactlyOnce},
        {"AllocationFailureAtomicity", &TestAllocationFailureAtomicity},
        {"BudgetsPeakAndReset", &TestBudgetsPeakAndReset},
        {"AllocationTagIsolationAndInvalidTag", &TestAllocationTagIsolationAndInvalidTag},
        {"CounterOverflowDoesNotWrap", &TestCounterOverflowDoesNotWrap},
        {"TrackingEnableDisableAndFrameResetContract", &TestTrackingEnableDisableAndFrameResetContract},
    });
}
