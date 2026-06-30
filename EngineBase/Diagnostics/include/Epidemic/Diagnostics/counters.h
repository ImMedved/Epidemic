#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace epidemic::diagnostics
{
enum class CounterId : std::uint8_t
{
    Frames,
    TasksScheduled,
    TasksCompleted,
    QueuedEvents,
    MemoryUsed,
    WorkerCount,
    FrameTimeMicros,
    Count,
};

[[nodiscard]] constexpr std::size_t CounterCount() noexcept
{
    return static_cast<std::size_t>(CounterId::Count);
}

[[nodiscard]] inline std::string_view ToString(CounterId counter_id) noexcept
{
    switch (counter_id)
    {
    case CounterId::Frames:
        return "frames";
    case CounterId::TasksScheduled:
        return "tasks_scheduled";
    case CounterId::TasksCompleted:
        return "tasks_completed";
    case CounterId::QueuedEvents:
        return "queued_events";
    case CounterId::MemoryUsed:
        return "memory_used";
    case CounterId::WorkerCount:
        return "worker_count";
    case CounterId::FrameTimeMicros:
        return "frame_time_micros";
    case CounterId::Count:
        break;
    }

    return "unknown";
}

class DiagnosticsCounters
{
  public:
    void Increment(CounterId counter_id, std::int64_t delta = 1) noexcept;
    void Decrement(CounterId counter_id, std::int64_t delta = 1) noexcept;
    void Set(CounterId counter_id, std::int64_t value) noexcept;
    [[nodiscard]] std::int64_t Get(CounterId counter_id) const noexcept;
    void Reset() noexcept;

  private:
    [[nodiscard]] static constexpr std::size_t ToIndex(CounterId counter_id) noexcept
    {
        return static_cast<std::size_t>(counter_id);
    }

    std::array<std::atomic<std::int64_t>, CounterCount()> values_{};
};

[[nodiscard]] DiagnosticsCounters &GlobalCounters() noexcept;
} // namespace epidemic::diagnostics
