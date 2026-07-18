#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Navigation/navigation_types.h"

#include <cstddef>
#include <memory>
#include <span>

namespace epidemic::runtime::navigation
{
// Public service contracts for the Navigation major. Implementations may be mock or backed by real nav data,
// but callers only see path queries, tile states and projection interfaces.

class INavCostProvider
{
public:
    virtual ~INavCostProvider() = default;

    [[nodiscard]] virtual float GetTraversalCost(const NavCostQuery& query) const = 0;
};

class INavigationBackend
{
public:
    virtual ~INavigationBackend() = default;

    [[nodiscard]] virtual foundation::Result<PathResult> BuildPath(const PathRequest& request, NavigationRevision revision) const = 0;
};

class INavigationDataSource
{
public:
    virtual ~INavigationDataSource() = default;

    [[nodiscard]] virtual NavigationRevision CurrentRevision(RegionId region) const = 0;
};

class IDynamicObstacleProjection
{
public:
    virtual ~IDynamicObstacleProjection() = default;

    [[nodiscard]] virtual std::span<const DynamicObstacle> ObstaclesForRegion(RegionId region) const = 0;
};

class INavigationObstacleSource
{
public:
    virtual ~INavigationObstacleSource() = default;

    [[nodiscard]] virtual std::span<const DynamicObstacle> ObstaclesForRegion(RegionId region) const = 0;
};

class INavTileRegistry
{
public:
    virtual ~INavTileRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterTile(NavTileId tile, NavTileState state) = 0;

    [[nodiscard]] virtual NavTileState GetTileState(NavTileId tile) const = 0;

    [[nodiscard]] virtual foundation::Result<void> MarkTileDirty(NavTileId tile) = 0;

    [[nodiscard]] virtual std::size_t RebuildDirtyTiles(RuntimeBudget budget) = 0;
};

class INavigationRuntime
{
public:
    virtual ~INavigationRuntime() = default;

    [[nodiscard]] virtual foundation::Result<PathQueryId> RequestPath(const PathRequest& request) = 0;
    [[nodiscard]] virtual foundation::Result<PathQueryHandle> RequestPathHandle(const PathRequest& request) = 0;

    [[nodiscard]] virtual foundation::Result<void> CancelPath(PathQueryId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> CancelPath(PathQueryHandle handle) = 0;

    [[nodiscard]] virtual std::size_t Tick(RuntimeBudget budget) = 0;

    [[nodiscard]] virtual PathQueryState GetPathState(PathQueryId id) const = 0;

    [[nodiscard]] virtual foundation::Result<PathResult> GetPathResult(PathQueryId id) const = 0;
    [[nodiscard]] virtual foundation::Result<PathResult> GetPathResult(PathQueryHandle handle) const = 0;
    [[nodiscard]] virtual foundation::Result<void> ReleasePathResult(PathQueryHandle handle) = 0;

    virtual void SetProjectionSources(const INavCostProvider* costs, const IDynamicObstacleProjection* obstacles) = 0;
    virtual void SetBackendSources(const INavigationBackend* backend, const INavigationDataSource* data_source) = 0;
};

struct NavigationServices
{
    std::shared_ptr<INavigationRuntime> runtime;
    std::shared_ptr<INavTileRegistry> tiles;
};

struct NavigationDependencies
{
    std::shared_ptr<INavigationBackend> backend;
    std::shared_ptr<INavigationDataSource> data_source;
    std::shared_ptr<INavigationObstacleSource> obstacle_source;
};

[[nodiscard]] std::unique_ptr<class NavigationRuntime> CreateNavigationRuntime(
    NavigationOptions options = {},
    NavigationDependencies dependencies = {});
[[nodiscard]] foundation::Result<NavigationServices> CreateNavigationServices(
    NavigationOptions options = {},
    NavigationDependencies dependencies = {});
[[nodiscard]] NavigationServices CreateMockNavigationServices(NavigationOptions options = {});
} // namespace epidemic::runtime::navigation
