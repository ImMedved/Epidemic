#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Navigation/navigation_types.h"

#include <cstddef>
#include <memory>
#include <span>

namespace epidemic::runtime::navigation
{
// File note:
// Public service contracts for the Navigation major. Implementations may be mock or backed by real nav data,
// but callers only see path queries, tile states and projection interfaces.

class INavCostProvider
{
public:
    virtual ~INavCostProvider() = default;

    // Function note: Returns traversal cost for a generic navigation query.
    // Inputs: region/surface/position query from Navigation; outputs: multiplier-like cost value.
    // Relations: consumed by path services, usually implemented by Environment or game-side projection adapters.
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

    // Function note: Returns current obstacle snapshots for a region.
    // Inputs: runtime region id; outputs: span of immutable obstacle projection records.
    // Relations: lets Navigation observe Physics/World-derived blockers without depending on their implementations.
    [[nodiscard]] virtual std::span<const DynamicObstacle> ObstaclesForRegion(RegionId region) const = 0;
};

class INavTileRegistry
{
public:
    virtual ~INavTileRegistry() = default;

    // Function note: Registers or replaces a tile state in the navigation registry.
    // Inputs: tile id and initial state; outputs: success/failure Result.
    // Relations: used by streaming/build pipelines before queries depend on tile availability.
    [[nodiscard]] virtual foundation::Result<void> RegisterTile(NavTileId tile, NavTileState state) = 0;

    // Function note: Reads the current state of a tile.
    // Inputs: tile id; outputs: state, or Missing for unknown ids.
    // Relations: pairs with MarkTileDirty and RebuildDirtyTiles during nav data lifecycle updates.
    [[nodiscard]] virtual NavTileState GetTileState(NavTileId tile) const = 0;

    // Function note: Marks a tile as requiring rebuild.
    // Inputs: tile id; outputs: success/failure Result.
    // Relations: called by projection adapters when world geometry or cost data changes.
    [[nodiscard]] virtual foundation::Result<void> MarkTileDirty(NavTileId tile) = 0;

    // Function note: Advances dirty/rebuilding tiles according to a runtime budget.
    // Inputs: budget limits; outputs: number of tile state transitions performed.
    // Relations: keeps rebuild work budgeted and independent from path query execution.
    [[nodiscard]] virtual std::size_t RebuildDirtyTiles(RuntimeBudget budget) = 0;
};

class INavigationRuntime
{
public:
    virtual ~INavigationRuntime() = default;

    // Function note: Submits a path query and returns its runtime handle id.
    // Inputs: start/target/region/budget hint; outputs: query id or validation error.
    // Relations: Tick advances query state; GetPathResult reads completed output.
    [[nodiscard]] virtual foundation::Result<PathQueryId> RequestPath(const PathRequest& request) = 0;
    [[nodiscard]] virtual foundation::Result<PathQueryHandle> RequestPathHandle(const PathRequest& request) = 0;

    // Function note: Cancels a pending or running query.
    // Inputs: path query id; outputs: success/failure Result.
    // Relations: cancelled queries remain observable through GetPathState but do not produce paths.
    [[nodiscard]] virtual foundation::Result<void> CancelPath(PathQueryId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> CancelPath(PathQueryHandle handle) = 0;

    // Function note: Advances query execution by a caller-provided budget.
    // Inputs: runtime budget; outputs: number of query transitions performed.
    // Relations: provides deterministic async-like behavior without a real navmesh backend.
    [[nodiscard]] virtual std::size_t Tick(RuntimeBudget budget) = 0;

    // Function note: Reads a query lifecycle state.
    // Inputs: query id; outputs: Failed for unknown/stale ids.
    // Relations: lightweight status check before retrieving completed path data.
    [[nodiscard]] virtual PathQueryState GetPathState(PathQueryId id) const = 0;

    // Function note: Retrieves the current path result for a query.
    // Inputs: query id; outputs: PathResult for completed queries, otherwise a Result failure.
    // Relations: depends on Tick-created path data and does not mutate query state.
    [[nodiscard]] virtual foundation::Result<PathResult> GetPathResult(PathQueryId id) const = 0;
    [[nodiscard]] virtual foundation::Result<PathResult> GetPathResult(PathQueryHandle handle) const = 0;
    [[nodiscard]] virtual foundation::Result<void> ReleasePathResult(PathQueryHandle handle) = 0;

    // Function note: Installs non-owning projection adapters used during path generation.
    // Inputs: nullable pointers that must outlive the runtime while registered; output: none.
    // Relations: keeps Navigation decoupled from Environment/Physics implementations.
    virtual void SetProjectionSources(const INavCostProvider* costs, const IDynamicObstacleProjection* obstacles) = 0;
    virtual void SetBackendSources(const INavigationBackend* backend, const INavigationDataSource* data_source) = 0;
};

struct NavigationServices
{
    std::shared_ptr<INavigationRuntime> runtime;
    std::shared_ptr<INavTileRegistry> tiles;
};

[[nodiscard]] std::unique_ptr<class NavigationRuntime> CreateNavigationRuntime(NavigationOptions options = {});
[[nodiscard]] NavigationServices CreateMockNavigationServices(NavigationOptions options = {});
} // namespace epidemic::runtime::navigation
