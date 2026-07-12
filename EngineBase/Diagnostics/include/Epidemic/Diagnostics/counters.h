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
    WorkerCount,
    CurrentFrameIndex,
    FrameTimeMicros,
    PlatformPumpTimeMicros,
    InputUpdateTimeMicros,
    ModuleTickTimeMicros,
    PresentTimeMicros,
    MainThreadTasksExecuted,
    EventBusQueuedEvents,
    EventBusDispatchedEvents,
    PlatformEventsThisFrame,
    InputEventsThisFrame,
    MemoryUsedBytes,
    MemoryPeakBytes,
    RhiFrames,
    RhiPresents,
    RhiResizeCount,
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
    case CounterId::WorkerCount:
        return "worker_count";
    case CounterId::CurrentFrameIndex:
        return "current_frame_index";
    case CounterId::FrameTimeMicros:
        return "frame_time_micros";
    case CounterId::PlatformPumpTimeMicros:
        return "platform_pump_time_micros";
    case CounterId::InputUpdateTimeMicros:
        return "input_update_time_micros";
    case CounterId::ModuleTickTimeMicros:
        return "module_tick_time_micros";
    case CounterId::PresentTimeMicros:
        return "present_time_micros";
    case CounterId::MainThreadTasksExecuted:
        return "main_thread_tasks_executed";
    case CounterId::EventBusQueuedEvents:
        return "event_bus_queued_events";
    case CounterId::EventBusDispatchedEvents:
        return "event_bus_dispatched_events";
    case CounterId::PlatformEventsThisFrame:
        return "platform_events_this_frame";
    case CounterId::InputEventsThisFrame:
        return "input_events_this_frame";
    case CounterId::MemoryUsedBytes:
        return "memory_used_bytes";
    case CounterId::MemoryPeakBytes:
        return "memory_peak_bytes";
    case CounterId::RhiFrames:
        return "rhi_frames";
    case CounterId::RhiPresents:
        return "rhi_presents";
    case CounterId::RhiResizeCount:
        return "rhi_resize_count";
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
} 