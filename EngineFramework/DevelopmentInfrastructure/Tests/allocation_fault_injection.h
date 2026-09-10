#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace epidemic::tests::allocation_fault
{
inline std::atomic<long long> allocations_before_failure{-1};

inline void Disable() noexcept
{
    allocations_before_failure.store(-1, std::memory_order_relaxed);
}

inline bool ShouldFail() noexcept
{
    auto remaining = allocations_before_failure.load(std::memory_order_relaxed);
    while (remaining >= 0)
    {
        if (remaining == 0)
        {
            allocations_before_failure.store(-1, std::memory_order_relaxed);
            return true;
        }
        if (allocations_before_failure.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed))
            return false;
    }
    return false;
}

class FailAfter
{
  public:
    explicit FailAfter(long long successful_allocations_before_failure) noexcept
    {
        allocations_before_failure.store(successful_allocations_before_failure, std::memory_order_relaxed);
    }
    ~FailAfter() { Disable(); }
    FailAfter(const FailAfter &) = delete;
    FailAfter &operator=(const FailAfter &) = delete;
};
} // namespace epidemic::tests::allocation_fault

void *operator new(std::size_t size)
{
    if (epidemic::tests::allocation_fault::ShouldFail())
        throw std::bad_alloc();
    if (void *memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}
