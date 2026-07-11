#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace epidemic::core
{
// This file defines the canonical frame-phase ordering used by Application.
// Support wiring and future upper layers hook into these phases to run work at stable points.

enum class FramePhase : std::uint8_t
{
    BeginFrame,
    PumpPlatformEvents,
    UpdateInput,
    DrainEvents,
    RunScheduledMainThreadTasks,
    TickModules,
    RhiBeginFrame,
    RhiEndFrame,
    Present,
    EndFrame,
    Count,
};

// Returns the number of phases in the canonical frame loop.
[[nodiscard]] constexpr std::size_t FramePhaseCount() noexcept
{
    return static_cast<std::size_t>(FramePhase::Count);
}

// Converts a frame phase to a stable name for logs and diagnostics.
[[nodiscard]] inline std::string_view ToString(FramePhase phase) noexcept
{
    switch (phase)
    {
    case FramePhase::BeginFrame:
        return "BeginFrame";
    case FramePhase::PumpPlatformEvents:
        return "PumpPlatformEvents";
    case FramePhase::UpdateInput:
        return "UpdateInput";
    case FramePhase::DrainEvents:
        return "DrainEvents";
    case FramePhase::RunScheduledMainThreadTasks:
        return "RunScheduledMainThreadTasks";
    case FramePhase::TickModules:
        return "TickModules";
    case FramePhase::RhiBeginFrame:
        return "RhiBeginFrame";
    case FramePhase::RhiEndFrame:
        return "RhiEndFrame";
    case FramePhase::Present:
        return "Present";
    case FramePhase::EndFrame:
        return "EndFrame";
    case FramePhase::Count:
        break;
    }

    return "Unknown";
}

inline constexpr std::array<FramePhase, FramePhaseCount()> kFramePhaseOrder{
    FramePhase::BeginFrame,
    FramePhase::PumpPlatformEvents,
    FramePhase::UpdateInput,
    FramePhase::DrainEvents,
    FramePhase::RunScheduledMainThreadTasks,
    FramePhase::TickModules,
    FramePhase::RhiBeginFrame,
    FramePhase::RhiEndFrame,
    FramePhase::Present,
    FramePhase::EndFrame,
};

// Returns the canonical ordered view consumed by Application::ExecuteFrame().
[[nodiscard]] inline constexpr std::span<const FramePhase> FramePhaseOrder() noexcept
{
    return std::span<const FramePhase>(kFramePhaseOrder.data(), kFramePhaseOrder.size());
}
} // namespace epidemic::core