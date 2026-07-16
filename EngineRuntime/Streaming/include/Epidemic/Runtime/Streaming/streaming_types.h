#pragma once

#include "Epidemic/Runtime/Foundation/runtime_budget.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace epidemic::runtime::streaming
{
// File note:
// Shared value types for the Streaming major. These types describe request identity,
// state, priority and budget without exposing any implementation details.

enum class StreamingState
{
    NotRequested,
    Requested,
    Queued,
    Loading,
    Loaded,
    Activating,
    Active,
    Resident,
    Deactivating,
    Unloading,
    Unloaded,
    Failed
};

enum class StreamingPriorityClass
{
    Critical,
    High,
    Normal,
    Low,
    Background
};

struct StreamingRequestId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] friend constexpr bool operator==(StreamingRequestId, StreamingRequestId) noexcept = default;
};

struct StreamingRequest
{
    StreamingRequestId id{};
    RegionId region{};
    ChunkId chunk{};
    StreamingPriorityClass priority = StreamingPriorityClass::Normal;
    RuntimeBudget budget_hint{};
};

struct StreamingBudget
{
    std::chrono::microseconds cpu_budget{};
    std::size_t max_requests = 0;
    std::size_t max_bytes = 0;

    [[nodiscard]] constexpr bool IsEmpty() const noexcept
    {
        return cpu_budget.count() == 0 && max_requests == 0 && max_bytes == 0;
    }
};

struct StreamingProgress
{
    StreamingRequestId id{};
    StreamingState state = StreamingState::NotRequested;
    float progress = 0.0f;
    std::uint64_t revision = 0;
};
} // namespace epidemic::runtime::streaming

template <>
struct std::hash<epidemic::runtime::streaming::StreamingRequestId>
{
    std::size_t operator()(epidemic::runtime::streaming::StreamingRequestId id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
