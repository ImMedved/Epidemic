#pragma once

#include "Epidemic/Runtime/Time/time_runtime.h"

#include <vector>

namespace epidemic::runtime::detail
{
[[nodiscard]] inline std::vector<PhaseBoundary> DefaultPhaseBoundaries()
{
    return {
        PhaseBoundary{0, DayPhase::Night},
        PhaseBoundary{5 * 60, DayPhase::Dawn},
        PhaseBoundary{8 * 60, DayPhase::Day},
        PhaseBoundary{18 * 60, DayPhase::Dusk},
        PhaseBoundary{21 * 60, DayPhase::Night},
    };
}
} // namespace epidemic::runtime::detail
