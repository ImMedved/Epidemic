#include "navigation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime::navigation
{
NavigationRuntime::NavigationRuntime(NavigationOptions options) : options_(options)
{
}

foundation::Result<void> NavigationRuntime::RegisterTile(NavTileId tile, NavTileState state)
{
    if (!tile.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.invalid_tile", "navigation tile id must be valid before registration"));
    }

    tiles_[tile] = state;
    return foundation::Result<void>::Success();
}

NavTileState NavigationRuntime::GetTileState(NavTileId tile) const
{
    const auto iterator = tiles_.find(tile);
    if (iterator == tiles_.end())
    {
        return NavTileState::Missing;
    }

    return iterator->second;
}

foundation::Result<void> NavigationRuntime::MarkTileDirty(NavTileId tile)
{
    if (!tile.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.invalid_tile", "navigation tile id must be valid before it can be dirtied"));
    }

    auto iterator = tiles_.find(tile);
    if (iterator == tiles_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.tile_not_found", "navigation tile was not registered"));
    }

    iterator->second = NavTileState::Dirty;
    return foundation::Result<void>::Success();
}

std::size_t NavigationRuntime::RebuildDirtyTiles(RuntimeBudget budget)
{
    const std::size_t limit = BudgetLimit(budget, tiles_.size());
    std::size_t transitioned = 0;

    for (const NavTileId tile : BuildTileWorkList())
    {
        if (transitioned >= limit)
        {
            break;
        }

        auto& state = tiles_[tile];
        if (state == NavTileState::Dirty)
        {
            state = NavTileState::Rebuilding;
            ++transitioned;
        }
        else if (state == NavTileState::Rebuilding)
        {
            state = NavTileState::Ready;
            ++transitioned;
        }
    }

    return transitioned;
}

foundation::Result<PathQueryId> NavigationRuntime::RequestPath(const PathRequest& request)
{
    if (!request.region.IsValid())
    {
        return foundation::Result<PathQueryId>::Failure(
            foundation::Error::Create("navigation.invalid_region", "path request must reference a valid region"));
    }

    if (!options_.enable_mock_queries)
    {
        return foundation::Result<PathQueryId>::Failure(
            foundation::Error::Create("navigation.queries_disabled", "mock path queries are disabled"));
    }

    const PathQueryId id{next_query_value_++};
    QueryRecord record{};
    record.request = request;
    record.result.state = PathQueryState::Pending;
    record.result.revision = 1;
    queries_.emplace(id, record);
    return foundation::Result<PathQueryId>::Success(id);
}

foundation::Result<void> NavigationRuntime::CancelPath(PathQueryId id)
{
    QueryRecord* query = FindQuery(id);
    if (query == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.query_not_found", "path query was not found for cancellation"));
    }

    if (query->result.state == PathQueryState::Completed || query->result.state == PathQueryState::Failed)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.query_terminal", "terminal path queries cannot be cancelled"));
    }

    if (query->result.state == PathQueryState::Cancelled)
    {
        return foundation::Result<void>::Success();
    }

    query->result.state = PathQueryState::Cancelled;
    query->result.points.clear();
    ++query->result.revision;
    return foundation::Result<void>::Success();
}

std::size_t NavigationRuntime::Tick(RuntimeBudget budget)
{
    const std::size_t limit = BudgetLimit(budget, queries_.size());
    std::size_t transitioned = 0;

    for (const PathQueryId id : BuildQueryWorkList())
    {
        if (transitioned >= limit)
        {
            break;
        }

        QueryRecord& query = queries_[id];
        if (query.result.state == PathQueryState::Pending)
        {
            query.result.state = PathQueryState::Running;
            ++query.result.revision;
            ++transitioned;
        }
        else if (query.result.state == PathQueryState::Running)
        {
            CompleteQuery(query);
            ++transitioned;
        }
    }

    return transitioned;
}

PathQueryState NavigationRuntime::GetPathState(PathQueryId id) const
{
    const QueryRecord* query = FindQuery(id);
    if (query == nullptr)
    {
        return PathQueryState::Failed;
    }

    return query->result.state;
}

foundation::Result<PathResult> NavigationRuntime::GetPathResult(PathQueryId id) const
{
    const QueryRecord* query = FindQuery(id);
    if (query == nullptr)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.query_not_found", "path query was not found"));
    }

    if (query->result.state != PathQueryState::Completed)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.query_not_completed", "path query has not completed"));
    }

    return foundation::Result<PathResult>::Success(query->result);
}

void NavigationRuntime::SetProjectionSources(const INavCostProvider* costs, const IDynamicObstacleProjection* obstacles)
{
    costs_ = costs;
    obstacles_ = obstacles;
}

std::size_t NavigationRuntime::BudgetLimit(RuntimeBudget budget, std::size_t fallback) const noexcept
{
    if (budget.HasItemBudget())
    {
        return budget.max_items;
    }

    return fallback;
}

NavigationRuntime::QueryRecord* NavigationRuntime::FindQuery(PathQueryId id)
{
    const auto iterator = queries_.find(id);
    if (iterator == queries_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const NavigationRuntime::QueryRecord* NavigationRuntime::FindQuery(PathQueryId id) const
{
    const auto iterator = queries_.find(id);
    if (iterator == queries_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

std::vector<NavTileId> NavigationRuntime::BuildTileWorkList() const
{
    std::vector<NavTileId> work_list;
    work_list.reserve(tiles_.size());
    for (const auto& [tile, state] : tiles_)
    {
        if (state == NavTileState::Dirty || state == NavTileState::Rebuilding)
        {
            work_list.push_back(tile);
        }
    }

    std::sort(work_list.begin(), work_list.end(), [](NavTileId left, NavTileId right) {
        return left.value < right.value;
    });
    return work_list;
}

std::vector<PathQueryId> NavigationRuntime::BuildQueryWorkList() const
{
    std::vector<PathQueryId> work_list;
    work_list.reserve(queries_.size());
    for (const auto& [id, query] : queries_)
    {
        if (query.result.state == PathQueryState::Pending || query.result.state == PathQueryState::Running)
        {
            work_list.push_back(id);
        }
    }

    std::sort(work_list.begin(), work_list.end(), [](PathQueryId left, PathQueryId right) {
        return left.value < right.value;
    });
    return work_list;
}

void NavigationRuntime::CompleteQuery(QueryRecord& query)
{
    if (obstacles_ != nullptr)
    {
        for (const DynamicObstacle& obstacle : obstacles_->ObstaclesForRegion(query.request.region))
        {
            if (obstacle.blocks_traversal)
            {
                query.result.points.push_back(query.request.start);
                query.result.points.push_back(Vec3{obstacle.bounds.max.x, query.request.start.y, obstacle.bounds.max.z});
                query.result.points.push_back(query.request.target);
                query.result.state = PathQueryState::Completed;
                ++query.result.revision;
                return;
            }
        }
    }

    const float cost = costs_ != nullptr ? costs_->GetTraversalCost(NavCostQuery{query.request.region, {}, query.request.start}) : 1.0f;
    query.result.points.push_back(query.request.start);
    if (cost > 1.0f)
    {
        query.result.points.push_back(Vec3{
            (query.request.start.x + query.request.target.x) * 0.5f,
            query.request.start.y,
            (query.request.start.z + query.request.target.z) * 0.5f});
    }
    query.result.points.push_back(query.request.target);
    query.result.state = PathQueryState::Completed;
    ++query.result.revision;
}
} // namespace epidemic::runtime::navigation
