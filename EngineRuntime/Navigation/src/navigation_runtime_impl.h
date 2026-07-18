#pragma once

#include "Epidemic/Runtime/Navigation/navigation_runtime.h"

#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>

namespace epidemic::runtime::navigation
{
// File note:
// Internal in-memory Navigation implementation used by the runtime foundation tests. It models lifecycle and budgets,
// not production navmesh generation.

class NavigationRuntime final : public INavigationRuntime, public INavTileRegistry
{
public:
    explicit NavigationRuntime(NavigationOptions options, NavigationDependencies dependencies = {});

    [[nodiscard]] foundation::Result<void> RegisterTile(NavTileId tile, NavTileState state) override;
    [[nodiscard]] NavTileState GetTileState(NavTileId tile) const override;
    [[nodiscard]] foundation::Result<void> MarkTileDirty(NavTileId tile) override;
    [[nodiscard]] std::size_t RebuildDirtyTiles(RuntimeBudget budget) override;

    [[nodiscard]] foundation::Result<PathQueryId> RequestPath(const PathRequest& request) override;
    [[nodiscard]] foundation::Result<PathQueryHandle> RequestPathHandle(const PathRequest& request) override;
    [[nodiscard]] foundation::Result<void> CancelPath(PathQueryId id) override;
    [[nodiscard]] foundation::Result<void> CancelPath(PathQueryHandle handle) override;
    [[nodiscard]] std::size_t Tick(RuntimeBudget budget) override;
    [[nodiscard]] PathQueryState GetPathState(PathQueryId id) const override;
    [[nodiscard]] foundation::Result<PathResult> GetPathResult(PathQueryId id) const override;
    [[nodiscard]] foundation::Result<PathResult> GetPathResult(PathQueryHandle handle) const override;
    [[nodiscard]] foundation::Result<void> ReleasePathResult(PathQueryHandle handle) override;
    void SetProjectionSources(const INavCostProvider* costs, const IDynamicObstacleProjection* obstacles) override;
    void SetBackendSources(const INavigationBackend* backend, const INavigationDataSource* data_source) override;

private:
    struct QueryRecord
    {
        PathQueryHandle handle{};
        PathRequest request{};
        PathResult result{};
        std::chrono::steady_clock::time_point completed_at{};
        bool released = false;
    };

    [[nodiscard]] std::size_t BudgetLimit(RuntimeBudget budget, std::size_t fallback) const noexcept;
    [[nodiscard]] bool HasBackend() const noexcept;
    [[nodiscard]] bool HasReferenceQueries() const noexcept;
    [[nodiscard]] const INavigationBackend* Backend() const noexcept;
    [[nodiscard]] const INavigationDataSource* DataSource() const noexcept;
    [[nodiscard]] const INavigationObstacleSource* ObstacleSource() const noexcept;
    [[nodiscard]] QueryRecord* FindQuery(PathQueryId id);
    [[nodiscard]] const QueryRecord* FindQuery(PathQueryId id) const;
    [[nodiscard]] std::vector<NavTileId> BuildTileWorkList() const;
    [[nodiscard]] std::vector<PathQueryId> BuildQueryWorkList() const;
    [[nodiscard]] bool HasExpired(const QueryRecord& query) const;
    [[nodiscard]] bool HasSourceRevisionChanged(const QueryRecord& query) const;
    [[nodiscard]] std::size_t EstimatedPathBytes(const QueryRecord& query) const;
    [[nodiscard]] bool HasPathByteBudget(RuntimeBudget budget, const QueryRecord& query) const;
    void MarkStale(QueryRecord& query);
    bool CompleteQuery(QueryRecord& query, RuntimeBudget budget);
    void CompleteWithResult(QueryRecord& query, PathResult result);
    void CompleteWithReference(QueryRecord& query);

    NavigationOptions options_{};
    NavigationDependencies dependencies_{};
    std::uint64_t next_query_value_ = 1;
    std::uint32_t next_generation_ = 1;
    std::uint64_t nav_revision_ = 0;
    std::unordered_map<NavTileId, NavTileState> tiles_;
    std::unordered_map<PathQueryId, QueryRecord> queries_;
    const INavCostProvider* costs_ = nullptr;
    const IDynamicObstacleProjection* obstacles_ = nullptr;
    const INavigationBackend* backend_ = nullptr;
    const INavigationDataSource* data_source_ = nullptr;
};
} // namespace epidemic::runtime::navigation
