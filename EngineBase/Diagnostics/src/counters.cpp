#include <Epidemic/Diagnostics/counters.h>

namespace epidemic::diagnostics
{
namespace
{
DiagnosticsCounters global_counters;
} // namespace

void DiagnosticsCounters::Increment(CounterId counter_id, std::int64_t delta) noexcept
{
    values_[ToIndex(counter_id)].fetch_add(delta, std::memory_order_relaxed);
}

void DiagnosticsCounters::Decrement(CounterId counter_id, std::int64_t delta) noexcept
{
    values_[ToIndex(counter_id)].fetch_sub(delta, std::memory_order_relaxed);
}

void DiagnosticsCounters::Set(CounterId counter_id, std::int64_t value) noexcept
{
    values_[ToIndex(counter_id)].store(value, std::memory_order_relaxed);
}

std::int64_t DiagnosticsCounters::Get(CounterId counter_id) const noexcept
{
    return values_[ToIndex(counter_id)].load(std::memory_order_relaxed);
}

void DiagnosticsCounters::Reset() noexcept
{
    for (auto &value : values_)
    {
        value.store(0, std::memory_order_relaxed);
    }
}

DiagnosticsCounters &GlobalCounters() noexcept
{
    return global_counters;
}
} // namespace epidemic::diagnostics
