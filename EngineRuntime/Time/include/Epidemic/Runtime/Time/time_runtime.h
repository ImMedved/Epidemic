#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Time/game_calendar.h"
#include "Epidemic/Runtime/Time/game_time.h"
#include "Epidemic/Runtime/Time/time_state.h"

namespace epidemic::runtime
{
class IGameClock
{
  public:
    virtual ~IGameClock() = default;

    [[nodiscard]] virtual GameTime Now() const = 0;
    [[nodiscard]] virtual GameDuration LastDelta() const = 0;
    [[nodiscard]] virtual float GetTimeScale() const = 0;
    [[nodiscard]] virtual bool IsPaused() const = 0;
    [[nodiscard]] virtual CalendarDate GetCalendarDate() const = 0;
    [[nodiscard]] virtual DayPhase GetDayPhase() const = 0;
};

class ITimeRuntime : public IGameClock
{
  public:
    ~ITimeRuntime() override = default;

    virtual void SetTimeScale(float scale) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    [[nodiscard]] virtual foundation::Result<void> Skip(GameDuration duration) = 0;
};
} // namespace epidemic::runtime
