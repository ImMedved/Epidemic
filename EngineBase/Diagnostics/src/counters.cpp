#include <Epidemic/Diagnostics/counters.h>

#include <limits>

namespace epidemic::diagnostics
{
namespace
{
DiagnosticsCounters global_counters;

[[nodiscard]] constexpr std::int64_t SaturatingAdd(std::int64_t value, std::int64_t delta) noexcept
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (delta > 0 && value > maximum - delta)
    {
        return maximum;
    }
    if (delta < 0 && value < minimum - delta)
    {
        return minimum;
    }
    return value + delta;
}

[[nodiscard]] constexpr std::int64_t SaturatingSubtract(std::int64_t value, std::int64_t delta) noexcept
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (delta > 0 && value < minimum + delta)
    {
        return minimum;
    }
    if (delta < 0 && value > maximum + delta)
    {
        return maximum;
    }
    return value - delta;
}

template <typename Operation>
void UpdateCounter(std::atomic<std::int64_t> &counter, Operation operation) noexcept
{
    auto current = counter.load(std::memory_order_relaxed);
    for (;;)
    {
        const auto next = operation(current);
        if (next == current || counter.compare_exchange_weak(current, next, std::memory_order_relaxed))
        {
            return;
        }
    }
}
} // namespace

void DiagnosticsCounters::Increment(CounterId counter_id, std::int64_t delta) noexcept
{
    if (!IsValidCounterId(counter_id))
    {
        return;
    }
    UpdateCounter(values_[ToIndex(counter_id)], [delta](std::int64_t current) { return SaturatingAdd(current, delta); });
}

void DiagnosticsCounters::Decrement(CounterId counter_id, std::int64_t delta) noexcept
{
    if (!IsValidCounterId(counter_id))
    {
        return;
    }
    UpdateCounter(values_[ToIndex(counter_id)], [delta](std::int64_t current) { return SaturatingSubtract(current, delta); });
}

void DiagnosticsCounters::Set(CounterId counter_id, std::int64_t value) noexcept
{
    if (!IsValidCounterId(counter_id))
    {
        return;
    }
    values_[ToIndex(counter_id)].store(value, std::memory_order_relaxed);
}

std::int64_t DiagnosticsCounters::Get(CounterId counter_id) const noexcept
{
    if (!IsValidCounterId(counter_id))
    {
        return 0;
    }
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
