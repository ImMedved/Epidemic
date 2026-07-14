#pragma once

#include "Epidemic/Runtime/Foundation/runtime_budget.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Scene/bounds.h"
#include "Epidemic/Runtime/Scene/transform.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace epidemic::runtime::navigation
{
// File note:
// Public value types for the Navigation major. The types describe navigation tiles,
// path query lifecycle, traversal costs and obstacle projections without encoding AI decisions or movement rules.

struct NavTileId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const NavTileId&) const noexcept = default;
};

struct PathQueryId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const PathQueryId&) const noexcept = default;
};

struct DynamicObstacleId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const DynamicObstacleId&) const noexcept = default;
};

enum class NavTileState
{
    Missing,
    Queued,
    Loading,
    Ready,
    Dirty,
    Rebuilding,
    Failed,
    Unloaded
};

enum class PathQueryState
{
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled
};

struct PathRequest
{
    Vec3 start{};
    Vec3 target{};
    RegionId region{};
    RuntimeBudget budget_hint{};
};

struct PathResult
{
    PathQueryState state = PathQueryState::Pending;
    std::vector<Vec3> points;
};

struct NavCostQuery
{
    RegionId region{};
    SurfaceId surface{};
    Vec3 position{};
};

struct DynamicObstacle
{
    DynamicObstacleId id{};
    RegionId region{};
    Aabb bounds{};
    bool blocks_traversal = true;
    float cost_multiplier = 1.0f;
};

struct NavigationOptions
{
    bool enable_mock_queries = true;
};
} // namespace epidemic::runtime::navigation

namespace std
{
template <> struct hash<epidemic::runtime::navigation::NavTileId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::navigation::NavTileId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::navigation::PathQueryId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::navigation::PathQueryId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::navigation::DynamicObstacleId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::navigation::DynamicObstacleId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};
} // namespace std
