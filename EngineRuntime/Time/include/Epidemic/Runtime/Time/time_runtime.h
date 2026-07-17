#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Time/game_calendar.h"
#include "Epidemic/Runtime/Time/game_time.h"
#include "Epidemic/Runtime/Time/time_events.h"
#include "Epidemic/Runtime/Time/time_scale.h"
#include "Epidemic/Runtime/Time/time_snapshot.h"

#include <chrono>
#include <memory>
#include <vector>

namespace epidemic::runtime
{
struct TimeAdvanceResult
{
    TimeSnapshot previous{};
    TimeSnapshot current{};
    std::vector<TimeEvent> events{};
};

struct PhaseBoundary
{
    std::uint32_t start_minute = 0;
    DayPhase phase = DayPhase::Night;
};

struct TimeOptions
{
    CalendarDefinition calendar{};
    std::int64_t game_ticks_per_real_second = 1;
    TimeScale initial_time_scale{};
    std::vector<PhaseBoundary> phase_boundaries{};
};

class IGameClock
{
  public:
    virtual ~IGameClock() = default;

    [[nodiscard]] virtual GameTimePoint Now() const = 0;
    [[nodiscard]] virtual GameDuration LastDelta() const = 0;
    [[nodiscard]] virtual TimeSnapshot GetSnapshot() const = 0;
};

class ITimeRuntime
{
  public:
    virtual ~ITimeRuntime() = default;

    [[nodiscard]] virtual foundation::Result<TimeAdvanceResult> Advance(std::chrono::microseconds real_delta) = 0;
    [[nodiscard]] virtual foundation::Result<void> Pause() = 0;
    [[nodiscard]] virtual foundation::Result<void> Resume() = 0;
    [[nodiscard]] virtual foundation::Result<void> SetTimeScale(TimeScale scale) = 0;
    [[nodiscard]] virtual foundation::Result<void> Skip(GameDuration duration) = 0;
};

struct TimeServices
{
    std::shared_ptr<IGameClock> clock;
    std::shared_ptr<ITimeRuntime> runtime;
};

[[nodiscard]] foundation::Result<void> ValidateTimeOptions(const TimeOptions& options);
[[nodiscard]] foundation::Result<TimeServices> CreateTimeServices(const TimeOptions& options = {});
} // namespace epidemic::runtime
