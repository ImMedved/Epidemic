#pragma once

#include "Epidemic/Runtime/Foundation/runtime_budget.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Foundation/string_id.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <variant>
#include <vector>

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
    Cancelled,
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

struct StreamingRequestHandle
{
    StreamingRequestId id{};
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return id.IsValid() && generation != 0;
    }

    [[nodiscard]] friend constexpr bool operator==(StreamingRequestHandle, StreamingRequestHandle) noexcept = default;
};

struct StreamingDemandId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] friend constexpr bool operator==(StreamingDemandId, StreamingDemandId) noexcept = default;
};

struct StreamingDemandHandle
{
    StreamingDemandId id{};
    StreamingRequestId request{};
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return id.IsValid() && request.IsValid() && generation != 0;
    }

    [[nodiscard]] friend constexpr bool operator==(StreamingDemandHandle, StreamingDemandHandle) noexcept = default;
};

struct ChunkStreamingTarget
{
    ChunkId chunk{};
};

struct RegionStreamingTarget
{
    RegionId region{};
};

struct AssetStreamingTarget
{
    AssetId asset{};
};

struct ObjectStreamingTarget
{
    RuntimeObjectId object{};
};

struct ResourceGroupStreamingTarget
{
    foundation::StringId group{};
};

using StreamingTarget = std::variant<
    ChunkStreamingTarget,
    RegionStreamingTarget,
    ObjectStreamingTarget,
    ResourceGroupStreamingTarget,
    AssetStreamingTarget>;

enum class StreamingPlanStep
{
    ResolveTarget,
    PrepareData,
    PrepareResources,
    Commit,
    Rollback,
    Release
};

struct ProgressiveLoadPlan
{
    std::vector<StreamingPlanStep> steps{};
    std::size_t cursor = 0;
};

struct StreamingCancellationToken
{
    bool requested = false;
    std::uint32_t generation = 0;
};

struct StreamingRequest
{
    StreamingRequestId id{};
    StreamingRequestHandle handle{};
    StreamingDemandHandle demand_handle{};
    StreamingTarget target{ChunkStreamingTarget{}};
    RegionId region{};
    ChunkId chunk{};
    StreamingPriorityClass priority = StreamingPriorityClass::Normal;
    std::uint32_t demand_count = 1;
    StreamingCancellationToken cancellation{};
    ProgressiveLoadPlan load_plan{};
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
    StreamingRequestHandle handle{};
    StreamingTarget target{ChunkStreamingTarget{}};
    StreamingState state = StreamingState::NotRequested;
    float progress = 0.0f;
    std::uint32_t demand_count = 0;
    std::uint64_t revision = 0;
};

struct StreamingStatistics
{
    std::uint64_t requested = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t committed = 0;
    std::uint64_t rolled_back = 0;
    std::uint64_t failed = 0;
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

template <>
struct std::hash<epidemic::runtime::streaming::StreamingDemandId>
{
    std::size_t operator()(epidemic::runtime::streaming::StreamingDemandId id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
