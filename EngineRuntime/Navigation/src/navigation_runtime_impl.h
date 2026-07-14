#pragma once

#include "Epidemic/Runtime/Navigation/navigation_runtime.h"

#include <unordered_map>

namespace epidemic::runtime::navigation
{
// File note:
// Internal in-memory Navigation implementation used by the runtime foundation tests. It models lifecycle and budgets,
// not production navmesh generation.

class NavigationRuntime final : public INavigationRuntime, public INavTileRegistry
{
public:
    explicit NavigationRuntime(NavigationOptions options);

    [[nodiscard]] foundation::Result<void> RegisterTile(NavTileId tile, NavTileState state) override;
    [[nodiscard]] NavTileState GetTileState(NavTileId tile) const override;
    [[nodiscard]] foundation::Result<void> MarkTileDirty(NavTileId tile) override;
    [[nodiscard]] std::size_t RebuildDirtyTiles(RuntimeBudget budget) override;

    [[nodiscard]] foundation::Result<PathQueryId> RequestPath(const PathRequest& request) override;
    [[nodiscard]] foundation::Result<void> CancelPath(PathQueryId id) override;
    [[nodiscard]] std::size_t Tick(RuntimeBudget budget) override;
    [[nodiscard]] PathQueryState GetPathState(PathQueryId id) const override;
    [[nodiscard]] foundation::Result<PathResult> GetPathResult(PathQueryId id) const override;
    void SetProjectionSources(const INavCostProvider* costs, const IDynamicObstacleProjection* obstacles) override;

private:
    struct QueryRecord
    {
        PathRequest request{};
        PathResult result{};
    };

    [[nodiscard]] std::size_t BudgetLimit(RuntimeBudget budget, std::size_t fallback) const noexcept;
    [[nodiscard]] QueryRecord* FindQuery(PathQueryId id);
    [[nodiscard]] const QueryRecord* FindQuery(PathQueryId id) const;
    void CompleteQuery(QueryRecord& query);

    NavigationOptions options_{};
    std::uint64_t next_query_value_ = 1;
    std::unordered_map<NavTileId, NavTileState> tiles_;
    std::unordered_map<PathQueryId, QueryRecord> queries_;
    const INavCostProvider* costs_ = nullptr;
    const IDynamicObstacleProjection* obstacles_ = nullptr;
};
} // namespace epidemic::runtime::navigation
