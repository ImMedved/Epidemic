#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace epidemic::diagnostics
{
// This file defines process-wide numeric counters used by the EngineBase diagnostics baseline.
// Counters are intentionally simple atomics: they provide cheap observability hooks for tests,
// logs, and smoke apps without introducing a full telemetry or metrics backend.

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

// Returns the number of counter slots required by CounterId.
[[nodiscard]] constexpr std::size_t CounterCount() noexcept
{
    return static_cast<std::size_t>(CounterId::Count);
}

// Converts a counter identifier into a stable snake_case name for logs and diagnostics dumps.
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

// Owns the atomic storage for all baseline diagnostic counters.
class DiagnosticsCounters
{
  public:
    // Adds delta to the specified counter.
    void Increment(CounterId counter_id, std::int64_t delta = 1) noexcept;

    // Subtracts delta from the specified counter.
    void Decrement(CounterId counter_id, std::int64_t delta = 1) noexcept;

    // Replaces the specified counter with an exact value.
    void Set(CounterId counter_id, std::int64_t value) noexcept;

    // Returns the current value of the requested counter.
    [[nodiscard]] std::int64_t Get(CounterId counter_id) const noexcept;

    // Resets every counter to zero.
    void Reset() noexcept;

  private:
    // Maps the public enum to the underlying array slot.
    [[nodiscard]] static constexpr std::size_t ToIndex(CounterId counter_id) noexcept
    {
        return static_cast<std::size_t>(counter_id);
    }

    std::array<std::atomic<std::int64_t>, CounterCount()> values_{};
};

// Returns the process-wide diagnostics counter registry.
// Relationship: core systems use this singleton-like accessor for low-friction instrumentation.
[[nodiscard]] DiagnosticsCounters &GlobalCounters() noexcept;
} // namespace epidemic::diagnostics