#include "Epidemic/Runtime/Time/time_runtime.h"

#include "time_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
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
} // namespace

foundation::Result<void> ValidateTimeOptions(const TimeOptions& options)
{
    if (options.game_ticks_per_real_second <= 0)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_options", "game ticks per real second must be positive"));
    }

    if (!std::isfinite(options.initial_time_scale) || options.initial_time_scale <= 0.0)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_scale", "initial time scale must be finite and greater than zero"));
    }

    if (options.calendar.hours_per_day == 0 || options.calendar.days_per_month == 0 ||
        options.calendar.months_per_year == 0)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_calendar", "calendar units must be positive"));
    }

    const std::uint32_t minutes_per_day = options.calendar.hours_per_day * 60;
    std::vector<PhaseBoundary> boundaries = options.phase_boundaries;
    std::sort(boundaries.begin(), boundaries.end(), [](const PhaseBoundary& left, const PhaseBoundary& right) {
        return left.start_minute < right.start_minute;
    });
    for (std::size_t index = 0; index < boundaries.size(); ++index)
    {
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
