#pragma once

#include "Epidemic/Runtime/Navigation/navigation_runtime.h"

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace epidemic::runtime::navigation
{
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

    [[nodiscard]] foundation::Result<PathQueryHandle> RequestPathHandle(const PathRequest& request) override;
    [[nodiscard]] foundation::Result<void> CancelPath(PathQueryHandle handle) override;
    [[nodiscard]] std::size_t Tick(RuntimeBudget budget) override;
    [[nodiscard]] foundation::Result<PathQueryState> GetPathState(PathQueryHandle handle) const override;
    [[nodiscard]] foundation::Result<PathResult> GetPathResult(PathQueryHandle handle) const override;
    [[nodiscard]] foundation::Result<void> ReleasePathResult(PathQueryHandle handle) override;

private:
    struct QueryRecord
    {
        PathQueryHandle handle{};
        PathRequest request{};
        PathResult result{};
        std::optional<std::uint64_t> source_revision{};
        std::chrono::steady_clock::time_point completed_at{};
        bool released = false;
    };

    [[nodiscard]] std::size_t BudgetLimit(RuntimeBudget budget, std::size_t fallback) const noexcept;
    [[nodiscard]] bool HasBackend() const noexcept;
    [[nodiscard]] bool HasReferenceQueries() const noexcept;
    [[nodiscard]] const INavigationBackend* Backend() const noexcept;
    [[nodiscard]] const INavigationDataSource* DataSource() const noexcept;
    [[nodiscard]] const INavCostProvider* CostProvider() const noexcept;
    [[nodiscard]] const INavigationObstacleSource* ObstacleSource() const noexcept;
    [[nodiscard]] QueryRecord* FindQuery(PathQueryId id);
    [[nodiscard]] const QueryRecord* FindQuery(PathQueryId id) const;
    [[nodiscard]] std::vector<NavTileId> BuildTileWorkList() const;
    [[nodiscard]] std::vector<PathQueryId> BuildQueryWorkList() const;
    [[nodiscard]] bool HasExpired(const QueryRecord& query) const;
    [[nodiscard]] foundation::Result<std::optional<std::uint64_t>> ReadSourceRevision(RegionId region) const;
    [[nodiscard]] foundation::Result<bool> HasSourceRevisionChanged(const QueryRecord& query) const;
    [[nodiscard]] foundation::Result<void> ValidateRequest(const PathRequest& request) const;
    [[nodiscard]] foundation::Result<void> PreflightGlobalRevision() const;
    [[nodiscard]] static foundation::Result<void> PreflightResultRevision(const QueryRecord& query);
    [[nodiscard]] static bool CanAdvanceRevisionValue(std::uint64_t revision) noexcept;
    [[nodiscard]] static std::uint64_t NextRevisionValue(std::uint64_t revision) noexcept;
    [[nodiscard]] std::size_t EstimatedPathBytes(const QueryRecord& query) const;
    [[nodiscard]] bool HasPathByteBudget(RuntimeBudget budget, const QueryRecord& query) const;
    [[nodiscard]] static bool IsTerminal(PathQueryState state) noexcept;
    [[nodiscard]] static bool IsValidInitialTileState(NavTileState state) noexcept;
    [[nodiscard]] static bool CanTransition(NavTileState from, NavTileState to) noexcept;
    [[nodiscard]] static bool IsValidPathResult(const PathRequest& request, const PathResult& result, NavigationRevision expected_revision) noexcept;
    void MarkStale(QueryRecord& query);
    void PurgeReleasedAndExpired();
    bool CompleteQuery(QueryRecord& query, RuntimeBudget budget);
    [[nodiscard]] foundation::Result<PathResult> BuildReferenceResult(const QueryRecord& query) const;
    [[nodiscard]] foundation::Result<void> CompleteWithResult(QueryRecord& query, PathResult result);
    [[nodiscard]] foundation::Result<void> CompleteWithFailure(QueryRecord& query);

    NavigationOptions options_{};
    NavigationDependencies dependencies_{};
    std::uint64_t next_query_value_ = 1;
    std::uint32_t next_generation_ = 1;
    std::uint64_t nav_revision_ = 0;
    std::unordered_map<NavTileId, NavTileState> tiles_;
    std::unordered_map<PathQueryId, QueryRecord> queries_;
};
} // namespace epidemic::runtime::navigation
