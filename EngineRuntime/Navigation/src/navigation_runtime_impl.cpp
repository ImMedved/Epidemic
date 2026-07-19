#include "navigation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace epidemic::runtime::navigation
{
NavigationRuntime::NavigationRuntime(NavigationOptions options, NavigationDependencies dependencies)
    : options_(options), dependencies_(std::move(dependencies))
{
}

foundation::Result<void> NavigationRuntime::RegisterTile(NavTileId tile, NavTileState state)
{
    if (!tile.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.invalid_tile", "navigation tile id must be valid before registration"));
    }
    if (!IsValidInitialTileState(state))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.invalid_tile_state", "navigation tile initial state is not valid for registration"));
    }
    if (tiles_.contains(tile))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.duplicate_tile", "navigation tile is already registered"));
    }

    tiles_.emplace(tile, state);
    ++nav_revision_;
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
    if (!CanTransition(iterator->second, NavTileState::Dirty))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.invalid_tile_transition", "navigation tile cannot be marked dirty from its current state"));
    }

    iterator->second = NavTileState::Dirty;
    ++nav_revision_;
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
            if (!CanTransition(state, NavTileState::Rebuilding))
            {
                continue;
            }
            state = NavTileState::Rebuilding;
            ++nav_revision_;
            ++transitioned;
        }
        else if (state == NavTileState::Rebuilding)
        {
            if (!CanTransition(state, NavTileState::Ready))
            {
                continue;
            }
            state = NavTileState::Ready;
            ++nav_revision_;
            ++transitioned;
        }
    }

    return transitioned;
}

foundation::Result<PathQueryHandle> NavigationRuntime::RequestPathHandle(const PathRequest& request)
{
    if (!request.region.IsValid())
    {
        return foundation::Result<PathQueryHandle>::Failure(
            foundation::Error::Create("navigation.invalid_region", "path request must reference a valid region"));
    }

    if (!HasBackend() && !HasReferenceQueries())
    {
        return foundation::Result<PathQueryHandle>::Failure(
            foundation::Error::Create("navigation.backend_missing", "navigation backend is required when reference queries are disabled"));
    }

    const PathQueryId id{next_query_value_++};
    const PathQueryHandle handle{id, next_generation_++};
    QueryRecord record{};
    record.handle = handle;
    record.request = request;
    record.result.handle = handle;
    record.result.state = PathQueryState::Pending;
    record.result.nav_revision = DataSource() != nullptr ? DataSource()->CurrentRevision(request.region).value : nav_revision_;
    if (request.source_revision != 0 && request.source_revision != record.result.nav_revision)
    {
        record.result.state = PathQueryState::Stale;
    }
    record.result.revision = 1;
    queries_.emplace(id, record);
    return foundation::Result<PathQueryHandle>::Success(handle);
}

foundation::Result<void> NavigationRuntime::CancelPath(PathQueryHandle handle)
{
    QueryRecord* query = FindQuery(handle.id);
    if (query == nullptr || query->handle.generation != handle.generation)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.stale_handle", "path query handle generation is stale"));
    }

    if (IsTerminal(query->result.state) && query->result.state != PathQueryState::Cancelled)
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
    PurgeReleasedAndExpired();
    const auto started_at = std::chrono::steady_clock::now();
    const std::size_t limit = BudgetLimit(budget, queries_.size());
    std::size_t transitioned = 0;

    for (const PathQueryId id : BuildQueryWorkList())
    {
        if (transitioned >= limit)
        {
            break;
        }
        if (budget.HasTimeBudget() && std::chrono::steady_clock::now() - started_at >= budget.max_time)
        {
            break;
        }

        QueryRecord& query = queries_[id];
        if (HasSourceRevisionChanged(query))
        {
            MarkStale(query);
            ++transitioned;
        }
        else if (query.result.state == PathQueryState::Pending)
        {
            query.result.state = PathQueryState::Running;
            ++query.result.revision;
            ++transitioned;
        }
        else if (query.result.state == PathQueryState::Running)
        {
            query.result.state = PathQueryState::PartiallyComplete;
            ++query.result.revision;
            ++transitioned;
        }
        else if (query.result.state == PathQueryState::PartiallyComplete && CompleteQuery(query, budget))
        {
            ++transitioned;
        }
    }

    return transitioned;
}

foundation::Result<PathQueryState> NavigationRuntime::GetPathState(PathQueryHandle handle) const
{
    const QueryRecord* query = FindQuery(handle.id);
    if (query == nullptr || query->handle.generation != handle.generation)
    {
        return foundation::Result<PathQueryState>::Failure(
            foundation::Error::Create("navigation.stale_handle", "path query handle generation is stale"));
    }

    if (HasExpired(*query))
    {
        return foundation::Result<PathQueryState>::Success(PathQueryState::Stale);
    }

    if (HasSourceRevisionChanged(*query))
    {
        return foundation::Result<PathQueryState>::Success(PathQueryState::Stale);
    }

    return foundation::Result<PathQueryState>::Success(query->result.state);
}

foundation::Result<PathResult> NavigationRuntime::GetPathResult(PathQueryHandle handle) const
{
    const QueryRecord* query = FindQuery(handle.id);
    if (query == nullptr || query->handle.generation != handle.generation)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.stale_result", "path result is stale or released"));
    }

    if (query->released || HasExpired(*query) || HasSourceRevisionChanged(*query) || query->result.state == PathQueryState::Stale)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.stale_result", "path result is stale or released"));
    }

    if (query->result.state != PathQueryState::Completed)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.query_not_completed", "path query has not completed"));
    }

    return foundation::Result<PathResult>::Success(query->result);
}

foundation::Result<void> NavigationRuntime::ReleasePathResult(PathQueryHandle handle)
{
    QueryRecord* query = FindQuery(handle.id);
    if (query == nullptr || query->handle.generation != handle.generation)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.stale_handle", "path query handle generation is stale"));
    }

    query->released = true;
    ++query->result.revision;
    queries_.erase(handle.id);
    return foundation::Result<void>::Success();
}

std::size_t NavigationRuntime::BudgetLimit(RuntimeBudget budget, std::size_t fallback) const noexcept
{
    if (budget.HasItemBudget())
    {
        return budget.max_items;
    }

    return fallback;
}

bool NavigationRuntime::HasBackend() const noexcept
{
    return Backend() != nullptr;
}

bool NavigationRuntime::HasReferenceQueries() const noexcept
{
    return options_.enable_mock_queries;
}

const INavigationBackend* NavigationRuntime::Backend() const noexcept
{
    return dependencies_.backend.get();
}

const INavigationDataSource* NavigationRuntime::DataSource() const noexcept
{
    return dependencies_.data_source.get();
}

const INavigationObstacleSource* NavigationRuntime::ObstacleSource() const noexcept
{
    return dependencies_.obstacle_source.get();
}

const INavCostProvider* NavigationRuntime::CostProvider() const noexcept
{
    return dependencies_.cost_provider.get();
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
        if (query.result.state == PathQueryState::Pending || query.result.state == PathQueryState::Running ||
            query.result.state == PathQueryState::PartiallyComplete)
        {
            work_list.push_back(id);
        }
    }

    std::sort(work_list.begin(), work_list.end(), [](PathQueryId left, PathQueryId right) {
        return left.value < right.value;
    });
    return work_list;
}

bool NavigationRuntime::HasExpired(const QueryRecord& query) const
{
    if (query.request.result_ttl.count() <= 0 || query.result.state != PathQueryState::Completed)
    {
        return false;
    }

    return std::chrono::steady_clock::now() - query.completed_at >= query.request.result_ttl;
}

bool NavigationRuntime::HasSourceRevisionChanged(const QueryRecord& query) const
{
    const INavigationDataSource* data_source = DataSource();
    if (data_source == nullptr || query.result.nav_revision == 0)
    {
        return false;
    }

    return data_source->CurrentRevision(query.request.region).value != query.result.nav_revision;
}

std::size_t NavigationRuntime::EstimatedPathBytes(const QueryRecord& query) const
{
    std::size_t points = 2;
    const INavigationObstacleSource* obstacle_source = ObstacleSource();
    if (obstacle_source != nullptr)
    {
        for (const DynamicObstacle& obstacle : obstacle_source->ObstaclesForRegion(query.request.region))
        {
            if (obstacle.blocks_traversal)
            {
                points = 3;
                break;
            }
        }
    }
    else if (const INavCostProvider* costs = CostProvider();
             costs != nullptr && costs->GetTraversalCost(NavCostQuery{query.request.region, {}, query.request.start}) > 1.0f)
    {
        points = 3;
    }

    return points * sizeof(Vec3);
}

bool NavigationRuntime::HasPathByteBudget(RuntimeBudget budget, const QueryRecord& query) const
{
    return !budget.HasByteBudget() || EstimatedPathBytes(query) <= budget.max_bytes;
}

bool NavigationRuntime::IsTerminal(PathQueryState state) noexcept
{
    return state == PathQueryState::Completed || state == PathQueryState::Failed ||
           state == PathQueryState::Cancelled || state == PathQueryState::Stale;
}

bool NavigationRuntime::IsValidInitialTileState(NavTileState state) noexcept
{
    return state == NavTileState::Queued || state == NavTileState::Loading ||
           state == NavTileState::Ready || state == NavTileState::Unloaded;
}

bool NavigationRuntime::CanTransition(NavTileState from, NavTileState to) noexcept
{
    if (from == to)
    {
        return true;
    }

    switch (from)
    {
    case NavTileState::Queued:
        return to == NavTileState::Loading || to == NavTileState::Unloaded;
    case NavTileState::Loading:
        return to == NavTileState::Ready || to == NavTileState::Failed || to == NavTileState::Unloaded;
    case NavTileState::Ready:
        return to == NavTileState::Dirty || to == NavTileState::Unloaded;
    case NavTileState::Dirty:
        return to == NavTileState::Rebuilding || to == NavTileState::Unloaded;
    case NavTileState::Rebuilding:
        return to == NavTileState::Ready || to == NavTileState::Failed || to == NavTileState::Unloaded;
    case NavTileState::Failed:
        return to == NavTileState::Dirty || to == NavTileState::Unloaded;
    case NavTileState::Unloaded:
        return to == NavTileState::Queued || to == NavTileState::Loading;
    case NavTileState::Missing:
        return false;
    }

    return false;
}

bool NavigationRuntime::IsValidPathResult(const PathRequest& request, const PathResult& result, NavigationRevision expected_revision) noexcept
{
    if (result.state != PathQueryState::Completed || result.nav_revision != expected_revision.value)
    {
        return false;
    }
    if (result.points.size() < 2)
    {
        return false;
    }
    for (const Vec3& point : result.points)
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            return false;
        }
    }
    return result.points.front() == request.start && result.points.back() == request.target;
}

void NavigationRuntime::MarkStale(QueryRecord& query)
{
    if (query.result.state != PathQueryState::Stale)
    {
        query.result.state = PathQueryState::Stale;
        query.result.points.clear();
        ++query.result.revision;
    }
}

void NavigationRuntime::PurgeReleasedAndExpired()
{
    std::vector<PathQueryId> removed;
    removed.reserve(queries_.size());
    for (const auto& [id, query] : queries_)
    {
        if (query.released || HasExpired(query))
        {
            removed.push_back(id);
        }
    }
    std::sort(removed.begin(), removed.end(), [](PathQueryId left, PathQueryId right) {
        return left.value < right.value;
    });
    for (PathQueryId id : removed)
    {
        queries_.erase(id);
    }
}

bool NavigationRuntime::CompleteQuery(QueryRecord& query, RuntimeBudget budget)
{
    if (!HasPathByteBudget(budget, query))
    {
        return false;
    }

    if (const INavigationBackend* backend = Backend(); backend != nullptr)
    {
        const auto result = backend->BuildPath(query.request, NavigationRevision{query.result.nav_revision});
        if (result)
        {
            if (!IsValidPathResult(query.request, result.Value(), NavigationRevision{query.result.nav_revision}))
            {
                query.result.state = PathQueryState::Failed;
                query.result.points.clear();
                ++query.result.revision;
                return true;
            }
            CompleteWithResult(query, result.Value());
            return true;
        }

        query.result.state = PathQueryState::Failed;
        query.result.points.clear();
        ++query.result.revision;
        return true;
    }

    CompleteWithReference(query);
    return true;
}

void NavigationRuntime::CompleteWithResult(QueryRecord& query, PathResult result)
{
    const auto handle = query.handle;
    const auto nav_revision = query.result.nav_revision;
    const auto revision = query.result.revision;
    query.result = std::move(result);
    query.result.handle = handle;
    query.result.state = PathQueryState::Completed;
    query.result.nav_revision = nav_revision;
    query.result.revision = revision + 1;
    query.completed_at = std::chrono::steady_clock::now();
}

void NavigationRuntime::CompleteWithReference(QueryRecord& query)
{
    const INavigationObstacleSource* obstacle_source = ObstacleSource();
    if (obstacle_source != nullptr)
    {
        for (const DynamicObstacle& obstacle : obstacle_source->ObstaclesForRegion(query.request.region))
        {
            if (obstacle.blocks_traversal)
            {
                query.result.points.push_back(query.request.start);
                query.result.points.push_back(Vec3{obstacle.bounds.max.x, query.request.start.y, obstacle.bounds.max.z});
                query.result.points.push_back(query.request.target);
                query.result.state = PathQueryState::Completed;
                ++query.result.revision;
                query.completed_at = std::chrono::steady_clock::now();
                return;
            }
        }
    }

    const INavCostProvider* costs = CostProvider();
    const float cost = costs != nullptr ? costs->GetTraversalCost(NavCostQuery{query.request.region, {}, query.request.start}) : 1.0f;
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
    query.completed_at = std::chrono::steady_clock::now();
}
} // namespace epidemic::runtime::navigation
