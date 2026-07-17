#pragma once

#include "Epidemic/Runtime/Time/time_runtime.h"

#include <vector>

namespace epidemic::runtime
{
class TimeRuntime final : public IGameClock, public ITimeRuntime
{
  public:
    explicit TimeRuntime(TimeOptions options = {});

    [[nodiscard]] GameTimePoint Now() const override;
    [[nodiscard]] GameDuration LastDelta() const override;
    [[nodiscard]] TimeSnapshot GetSnapshot() const override;

    [[nodiscard]] foundation::Result<TimeAdvanceResult> Advance(std::chrono::microseconds real_delta) override;
    [[nodiscard]] foundation::Result<void> Pause() override;
    [[nodiscard]] foundation::Result<void> Resume() override;
    [[nodiscard]] foundation::Result<void> SetTimeScale(TimeScale scale) override;
    [[nodiscard]] foundation::Result<void> Skip(GameDuration duration) override;

    [[nodiscard]] const std::vector<TimeEvent>& GetEvents() const;
    [[nodiscard]] foundation::Result<GameTimePoint> ToGameTimePoint(CalendarDate date) const;
    [[nodiscard]] CalendarDate ToCalendarDate(GameTimePoint time) const;

  private:
    [[nodiscard]] foundation::Result<void> ValidateOptions() const;
    [[nodiscard]] foundation::Result<void> ValidateDate(CalendarDate date) const;
    [[nodiscard]] DayPhase DetermineDayPhase(const CalendarDate& date) const;
    [[nodiscard]] TimeAdvanceResult MakeResult(TimeSnapshot previous) const;

    void RefreshSnapshot(bool changed);
    void PushEvent(TimeEventKind kind);
    void AppendBoundaryEvents(const TimeSnapshot& previous);
    void ClearEvents();

    TimeOptions options_{};
    GameTimePoint now_{};
    GameDuration last_delta_{};
    TimeScale time_scale_{};
    bool paused_ = false;
    TimeRuntimeState state_ = TimeRuntimeState::Running;
    TimeSnapshot snapshot_{};
    std::vector<TimeEvent> events_;
    long double tick_remainder_ = 0.0L;
    std::uint64_t revision_ = 0;
};
} // namespace epidemic::runtime
