#include "Epidemic/Runtime/Time/time_runtime.h"

#include "time_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Error MakeTimeError(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] bool IsValidTimeScale(TimeScale scale)
{
    return scale.numerator > 0 && scale.denominator > 0;
}

[[nodiscard]] bool IsValidDayPhase(DayPhase phase) noexcept
{
    switch (phase)
    {
    case DayPhase::Dawn:
    case DayPhase::Day:
    case DayPhase::Dusk:
    case DayPhase::Night:
        return true;
    }
    return false;
}
} // namespace

foundation::Result<void> ValidateTimeOptions(const TimeOptions& options)
{
    if (options.game_ticks_per_real_second <= 0)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_options", "game ticks per real second must be positive"));
    }

    if (!IsValidTimeScale(options.initial_time_scale))
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_scale", "initial time scale numerator and denominator must be positive"));
    }

    if (options.calendar.hours_per_day == 0 || options.calendar.days_per_month == 0 ||
        options.calendar.months_per_year == 0)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_calendar", "calendar units must be positive"));
    }

    if (options.calendar.hours_per_day > (std::numeric_limits<std::uint32_t>::max() / 60))
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.overflow", "calendar day length overflows phase boundary validation"));
    }
    if (options.calendar.days_per_month >
        (std::numeric_limits<std::int64_t>::max() / options.calendar.months_per_year))
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.overflow", "calendar year length overflows time conversion"));
    }

    const std::uint32_t minutes_per_day = options.calendar.hours_per_day * 60;
    std::vector<PhaseBoundary> boundaries = options.phase_boundaries;
    std::sort(boundaries.begin(), boundaries.end(), [](const PhaseBoundary& left, const PhaseBoundary& right) {
        return left.start_minute < right.start_minute;
    });
    for (std::size_t index = 0; index < boundaries.size(); ++index)
    {
        if (!IsValidDayPhase(boundaries[index].phase))
        {
            return foundation::Result<void>::Failure(
                MakeTimeError("time.invalid_phase", "day phase boundary phase is outside the DayPhase enum domain"));
        }
        if (boundaries[index].start_minute >= minutes_per_day)
        {
            return foundation::Result<void>::Failure(
                MakeTimeError("time.invalid_phase_boundary", "day phase boundary is outside the configured day"));
        }

        if (index > 0 && boundaries[index - 1].start_minute == boundaries[index].start_minute)
        {
            return foundation::Result<void>::Failure(
                MakeTimeError("time.duplicate_phase_boundary", "day phase boundaries must have unique start minutes"));
        }
    }

    return foundation::Result<void>::Success();
}

foundation::Result<TimeServices> CreateTimeServices(const TimeOptions& options)
{
    const auto valid = ValidateTimeOptions(options);
    if (!valid)
    {
        return foundation::Result<TimeServices>::Failure(valid.GetError());
    }

    auto runtime = std::make_shared<TimeRuntime>(options);
    TimeServices services{};
    services.clock = runtime;
    services.runtime = runtime;
    return foundation::Result<TimeServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
