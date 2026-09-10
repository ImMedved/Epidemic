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
    [[nodiscard]] foundation::Result<TimeAdvanceResult> Skip(GameDuration duration) override;

    [[nodiscard]] const std::vector<TimeEvent>& GetEvents() const;
    [[nodiscard]] foundation::Result<GameTimePoint> ToGameTimePoint(CalendarDate date) const;
    [[nodiscard]] CalendarDate ToCalendarDate(GameTimePoint time) const;

  private:
    [[nodiscard]] foundation::Result<void> ValidateOptions() const;
    [[nodiscard]] foundation::Result<void> ValidateDate(CalendarDate date) const;
    [[nodiscard]] DayPhase DetermineDayPhase(const CalendarDate& date) const;
    struct TimeMutableState
    {
        TimeOptions options{};
        GameTimePoint now{};
        GameDuration last_delta{};
        TimeScale time_scale{};
        bool paused = false;
        TimeRuntimeState state = TimeRuntimeState::Running;
        TimeSnapshot snapshot{};
        std::vector<TimeEvent> events{};
        std::int64_t tick_remainder_numerator = 0;
        std::uint64_t revision = 0;
    };

    [[nodiscard]] TimeAdvanceResult MakeResult(TimeSnapshot previous) const;
    [[nodiscard]] foundation::Result<TimeSnapshot> BuildSnapshot(const TimeMutableState& state, bool changed) const;
    [[nodiscard]] foundation::Result<void> PreflightRevision(bool changed) const;
    [[nodiscard]] static bool IsValidDayPhase(DayPhase phase) noexcept;
    [[nodiscard]] static std::uint64_t NextRevision(std::uint64_t current) noexcept;
    [[nodiscard]] static foundation::Result<void> AppendBoundaryEvents(TimeMutableState& candidate, const TimeSnapshot& previous);
    [[nodiscard]] static foundation::Result<void> PushEvent(TimeMutableState& candidate, TimeEventKind kind);
    void Commit(TimeMutableState candidate) noexcept;

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
    std::int64_t tick_remainder_numerator_ = 0;
    std::uint64_t revision_ = 0;
};
} // namespace epidemic::runtime
