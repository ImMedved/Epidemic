#include <Epidemic/Diagnostics/counters.h>

namespace epidemic::diagnostics
{
// This file implements the process-wide diagnostics counter registry.
// The implementation stays deliberately small: relaxed atomics are enough because these counters
// are used for observability rather than for synchronization.

namespace
{
DiagnosticsCounters global_counters;
} // namespace

// Adds delta to the requested counter using relaxed atomic ordering.
void DiagnosticsCounters::Increment(CounterId counter_id, std::int64_t delta) noexcept
{
    values_[ToIndex(counter_id)].fetch_add(delta, std::memory_order_relaxed);
}

// Subtracts delta from the requested counter using relaxed atomic ordering.
void DiagnosticsCounters::Decrement(CounterId counter_id, std::int64_t delta) noexcept
{
    values_[ToIndex(counter_id)].fetch_sub(delta, std::memory_order_relaxed);
}

// Replaces the counter value using relaxed atomic ordering.
void DiagnosticsCounters::Set(CounterId counter_id, std::int64_t value) noexcept
{
    values_[ToIndex(counter_id)].store(value, std::memory_order_relaxed);
}

// Reads the current counter value using relaxed atomic ordering.
std::int64_t DiagnosticsCounters::Get(CounterId counter_id) const noexcept
{
    return values_[ToIndex(counter_id)].load(std::memory_order_relaxed);
}

// Clears every counter back to zero.
void DiagnosticsCounters::Reset() noexcept
{
    for (auto &value : values_)
    {
        value.store(0, std::memory_order_relaxed);
    }
}

// Returns the global counter storage used across the current process.
DiagnosticsCounters &GlobalCounters() noexcept
{
    return global_counters;
}
} 
