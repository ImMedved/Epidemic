#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace epidemic::core
{
// These phases are orchestrated on the main thread. Platform pumping, input publication,
// module tick entry points, and present hooks all plug into this order.
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

[[nodiscard]] constexpr std::size_t FramePhaseCount() noexcept
{
    return static_cast<std::size_t>(FramePhase::Count);
}

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

[[nodiscard]] inline constexpr std::span<const FramePhase> FramePhaseOrder() noexcept
{
    return std::span<const FramePhase>(kFramePhaseOrder.data(), kFramePhaseOrder.size());
}
} // namespace epidemic::core
