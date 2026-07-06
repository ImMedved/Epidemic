#include "../test_assert.h"

#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/allocator.h>
#include <Epidemic/Memory/imemory_tracker.h>
#include <Epidemic/Memory/memory_tracker.h>

#include <cstddef>

namespace
{
using epidemic::tests::Assert;

void TestMemoryBaseline()
{
    using epidemic::memory::AllocationTag;
    using epidemic::memory::DefaultAllocator;
    using epidemic::memory::MemoryTracker;
    using epidemic::memory::TrackingAllocator;

    MemoryTracker concrete_tracker(true);
    epidemic::memory::IMemoryTracker &tracker = concrete_tracker;
    tracker.RecordAllocate(AllocationTag::Core, 64);
    tracker.RecordAllocate(AllocationTag::Core, 16);
    tracker.RecordAllocate(AllocationTag::Platform, 32);

    const auto core_stats = tracker.GetStatistics(AllocationTag::Core);
    Assert(core_stats.allocated_bytes == 80, "Memory tracker must accumulate per-tag usage");
    Assert(core_stats.peak_allocated_bytes == 80, "Memory tracker must track peak usage");
    Assert(core_stats.allocation_count == 2, "Memory tracker must count allocations per tag");

    tracker.RecordFree(AllocationTag::Core, 48);
    Assert(tracker.GetUsage(AllocationTag::Core) == 32, "Memory tracker must reduce usage on free");

    tracker.SetBudget(AllocationTag::Core, 24);
    Assert(tracker.IsOverBudget(AllocationTag::Core), "Memory tracker must detect over-budget state");

    tracker.SetTrackingEnabled(false);
    tracker.RecordAllocate(AllocationTag::Tests, 128);
    Assert(tracker.GetStatistics(AllocationTag::Tests).allocated_bytes == 0,
           "Disabled tracking mode must ignore allocation accounting");

    tracker.SetTrackingEnabled(true);
    DefaultAllocator default_allocator;
    TrackingAllocator tracking_allocator(concrete_tracker, default_allocator);
    void *pointer = tracking_allocator.Allocate(40, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(pointer != nullptr, "Tracking allocator must return allocated memory");
    Assert(tracker.GetStatistics(AllocationTag::Tests).allocated_bytes == 40,
           "Tracking allocator must report allocations to the tracker");
    tracking_allocator.Deallocate(pointer, 40, alignof(std::max_align_t), AllocationTag::Tests);

    void *zero_pointer = tracking_allocator.Allocate(0, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(zero_pointer != nullptr, "Tracking allocator must materialize zero-sized allocations through the upstream");
    Assert(tracker.GetStatistics(AllocationTag::Tests).allocated_bytes == 1,
           "Tracking allocator must normalize zero-sized allocations to one tracked byte");
    tracking_allocator.Deallocate(zero_pointer, 0, alignof(std::max_align_t), AllocationTag::Tests);
}
}

int main()
{
    return epidemic::tests::RunNamedTests({{"MemoryBaseline", &TestMemoryBaseline}});
}