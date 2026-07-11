#pragma once

#include "Epidemic/Runtime/Time/time_events.h"
#include "Epidemic/Runtime/Time/time_runtime.h"
#include "Epidemic/Runtime/Time/time_snapshot.h"

#include <vector>

namespace epidemic::runtime
{
class TimeRuntime final : public ITimeRuntime
{
  public:
    TimeRuntime();

    [[nodiscard]] GameTime Now() const override;
    [[nodiscard]] GameDuration LastDelta() const override;
    [[nodiscard]] float GetTimeScale() const override;
    [[nodiscard]] bool IsPaused() const override;
    [[nodiscard]] CalendarDate GetCalendarDate() const override;
    [[nodiscard]] DayPhase GetDayPhase() const override;

    void SetTimeScale(float scale) override;
    void Pause() override;
    void Resume() override;
    [[nodiscard]] foundation::Result<void> Skip(GameDuration duration) override;

    void Update(GameDuration real_delta);

    [[nodiscard]] TimeSnapshot GetSnapshot() const;
    [[nodiscard]] const std::vector<TimeEvent>& GetEvents() const;

  private:
    [[nodiscard]] static CalendarDate ToCalendarDate(GameTime time) noexcept;
    [[nodiscard]] static DayPhase DetermineDayPhase(const CalendarDate& date) noexcept;

    void RefreshSnapshot();
    void PushEvent(TimeEventKind kind);
    void AppendBoundaryEvents(CalendarDate previous_date, DayPhase previous_phase);
    void ClearEvents();

    GameTime now_{};
    GameDuration last_delta_{};
    float time_scale_ = 1.0f;
    bool paused_ = false;
    TimeRuntimeState state_ = TimeRuntimeState::Running;
    TimeSnapshot snapshot_{};
    std::vector<TimeEvent> events_;
};
} // namespace epidemic::runtime
