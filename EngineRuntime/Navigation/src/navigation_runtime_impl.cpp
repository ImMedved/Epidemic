#include "navigation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <utility>

namespace epidemic::runtime::navigation
{
namespace
{
[[nodiscard]] foundation::Error NavError(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> NavFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(NavError(code, message));
}

[[nodiscard]] bool IsFinitePoint(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Vec3 SafeMidpoint(Vec3 left, Vec3 right) noexcept
{
    return Vec3{std::midpoint(left.x, right.x), left.y, std::midpoint(left.z, right.z)};
}
} // namespace

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
    if (auto revision = PreflightGlobalRevision(); !revision)
    {
        return revision;
    }

    try
    {
        tiles_.emplace(tile, state);
    }
    catch (const std::bad_alloc&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.out_of_memory", "navigation tile registry could not allocate tile record"));
    }
    nav_revision_ = NextRevisionValue(nav_revision_);
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
    if (iterator->second == NavTileState::Dirty)
    {
        return foundation::Result<void>::Success();
    }
    if (auto revision = PreflightGlobalRevision(); !revision)
    {
        return revision;
    }

    iterator->second = NavTileState::Dirty;
    nav_revision_ = NextRevisionValue(nav_revision_);
    return foundation::Result<void>::Success();
}

std::size_t NavigationRuntime::RebuildDirtyTiles(RuntimeBudget budget)
{
    const std::size_t limit = BudgetLimit(budget, tiles_.size());
    std::size_t transitioned = 0;

    for (const NavTileId tile : BuildTileWorkList())
    {
        if (transitioned >= limit || !CanAdvanceRevisionValue(nav_revision_))
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
            nav_revision_ = NextRevisionValue(nav_revision_);
            ++transitioned;
        }
        else if (state == NavTileState::Rebuilding)
        {
            if (!CanTransition(state, NavTileState::Ready))
            {
                continue;
            }
            state = NavTileState::Ready;
            nav_revision_ = NextRevisionValue(nav_revision_);
            ++transitioned;
        }
    }

    return transitioned;
}

foundation::Result<PathQueryHandle> NavigationRuntime::RequestPathHandle(const PathRequest& request)
{
    if (auto valid = ValidateRequest(request); !valid)
    {
        return foundation::Result<PathQueryHandle>::Failure(valid.GetError());
    }

    if (!HasBackend() && !HasReferenceQueries())
    {
        return foundation::Result<PathQueryHandle>::Failure(
            foundation::Error::Create("navigation.backend_missing", "navigation backend is required when reference queries are disabled"));
    }
    if (!CanAllocateMonotonicId(next_query_value_))
    {
        return NavFailure<PathQueryHandle>("navigation.query_id_exhausted", "path query id allocator is exhausted");
    }
    if (!CanAllocateMonotonicId(next_generation_))
    {
        return NavFailure<PathQueryHandle>("navigation.query_generation_exhausted", "path query generation allocator is exhausted");
    }

    const auto source_revision = ReadSourceRevision(request.region);
    if (!source_revision)
    {
        return foundation::Result<PathQueryHandle>::Failure(source_revision.GetError());
    }

    const PathQueryId id{next_query_value_};
    const PathQueryHandle handle{id, next_generation_};
    QueryRecord record{};
    record.handle = handle;
    record.request = request;
    record.result.handle = handle;
    record.result.state = PathQueryState::Pending;
    record.source_revision = source_revision.Value();
    record.result.nav_revision = record.source_revision.value_or(nav_revision_);
    if (request.source_revision != 0 && request.source_revision != record.result.nav_revision)
    {
        record.result.state = PathQueryState::Stale;
    }
    record.result.revision = 1;

    try
    {
        const auto [_, inserted] = queries_.emplace(id, std::move(record));
        if (!inserted)
        {
            return foundation::Result<PathQueryHandle>::Failure(
                foundation::Error::Create("navigation.duplicate_query_id", "allocated path query id already exists"));
        }
    }
    catch (const std::bad_alloc&)
    {
        return NavFailure<PathQueryHandle>("navigation.out_of_memory", "navigation query registry could not allocate query record");
    }

    next_query_value_ = next_query_value_ == std::numeric_limits<std::uint64_t>::max() ? 0 : next_query_value_ + 1;
    next_generation_ = next_generation_ == std::numeric_limits<std::uint32_t>::max() ? 0 : next_generation_ + 1;
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
    if (auto revision = PreflightResultRevision(*query); !revision)
    {
        return revision;
    }

    query->result.state = PathQueryState::Cancelled;
    query->result.points.clear();
    query->result.revision = NextRevisionValue(query->result.revision);
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
        const auto changed = HasSourceRevisionChanged(query);
        if (!changed)
        {
            if (auto failed = CompleteWithFailure(query); failed)
            {
                ++transitioned;
            }
            continue;
        }
        if (changed.Value())
        {
            MarkStale(query);
            ++transitioned;
        }
        else if (query.result.state == PathQueryState::Pending)
        {
            if (!CanAdvanceRevisionValue(query.result.revision))
            {
                auto failed = CompleteWithFailure(query);
                (void)failed;
                ++transitioned;
                continue;
            }
            query.result.state = PathQueryState::Running;
            query.result.revision = NextRevisionValue(query.result.revision);
            ++transitioned;
        }
        else if (query.result.state == PathQueryState::Running)
        {
            if (!CanAdvanceRevisionValue(query.result.revision))
            {
                auto failed = CompleteWithFailure(query);
                (void)failed;
                ++transitioned;
                continue;
            }
            query.result.state = PathQueryState::PartiallyComplete;
            query.result.revision = NextRevisionValue(query.result.revision);
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

    const auto changed = HasSourceRevisionChanged(*query);
    if (!changed)
    {
        return foundation::Result<PathQueryState>::Failure(changed.GetError());
    }
    if (changed.Value())
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

    const auto changed = HasSourceRevisionChanged(*query);
    if (!changed)
    {
        return foundation::Result<PathResult>::Failure(changed.GetError());
    }
    if (query->released || HasExpired(*query) || changed.Value() || query->result.state == PathQueryState::Stale)
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
    if (auto revision = PreflightResultRevision(*query); !revision)
    {
        return revision;
    }

    query->released = true;
    query->result.revision = NextRevisionValue(query->result.revision);
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

foundation::Result<std::optional<std::uint64_t>> NavigationRuntime::ReadSourceRevision(RegionId region) const
{
    const INavigationDataSource* data_source = DataSource();
    if (data_source == nullptr)
    {
        return foundation::Result<std::optional<std::uint64_t>>::Success(std::nullopt);
    }
    try
    {
        return foundation::Result<std::optional<std::uint64_t>>::Success(data_source->CurrentRevision(region).value);
    }
    catch (const std::exception& error)
    {
        return foundation::Result<std::optional<std::uint64_t>>::Failure(
            foundation::Error::Create("navigation.provider_exception", "navigation data source threw", error.what()));
    }
    catch (...)
    {
        return foundation::Result<std::optional<std::uint64_t>>::Failure(
            foundation::Error::Create("navigation.provider_exception", "navigation data source threw an unknown exception"));
    }
}

foundation::Result<bool> NavigationRuntime::HasSourceRevisionChanged(const QueryRecord& query) const
{
    if (!query.source_revision.has_value())
    {
        return foundation::Result<bool>::Success(false);
    }
    const auto current = ReadSourceRevision(query.request.region);
    if (!current)
    {
        return foundation::Result<bool>::Failure(current.GetError());
    }
    if (!current.Value().has_value())
    {
        return foundation::Result<bool>::Success(false);
    }
    return foundation::Result<bool>::Success(current.Value().value() != query.source_revision.value());
}

foundation::Result<void> NavigationRuntime::ValidateRequest(const PathRequest& request) const
{
    if (!request.region.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.invalid_region", "path request must reference a valid region"));
    }
    if (!IsFinitePoint(request.start) || !IsFinitePoint(request.target))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.invalid_coordinate", "path request coordinates must be finite"));
    }
    if (request.result_ttl.count() < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.invalid_ttl", "path result ttl must not be negative"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationRuntime::PreflightGlobalRevision() const
{
    if (!CanAdvanceRevisionValue(nav_revision_))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.revision_exhausted", "navigation revision is exhausted"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationRuntime::PreflightResultRevision(const QueryRecord& query)
{
    if (!CanAdvanceRevisionValue(query.result.revision))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("navigation.result_revision_exhausted", "path query result revision is exhausted"));
    }
    return foundation::Result<void>::Success();
}

bool NavigationRuntime::CanAdvanceRevisionValue(std::uint64_t revision) noexcept
{
    return revision != std::numeric_limits<std::uint64_t>::max();
}

std::uint64_t NavigationRuntime::NextRevisionValue(std::uint64_t revision) noexcept
{
    return revision + 1;
}

std::size_t NavigationRuntime::EstimatedPathBytes(const QueryRecord& query) const
{
    std::size_t points = 2;
    const INavigationObstacleSource* obstacle_source = ObstacleSource();
    try
    {
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
    }
    catch (...)
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
        if (!IsFinitePoint(point))
        {
            return false;
        }
    }
    return result.points.front() == request.start && result.points.back() == request.target;
}

void NavigationRuntime::MarkStale(QueryRecord& query)
{
    if (query.result.state != PathQueryState::Stale && CanAdvanceRevisionValue(query.result.revision))
    {
        query.result.state = PathQueryState::Stale;
        query.result.points.clear();
        query.result.revision = NextRevisionValue(query.result.revision);
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
        try
        {
            const auto result = backend->BuildPath(query.request, NavigationRevision{query.result.nav_revision});
            if (result)
            {
                if (!IsValidPathResult(query.request, result.Value(), NavigationRevision{query.result.nav_revision}))
                {
                    return static_cast<bool>(CompleteWithFailure(query));
                }
                return static_cast<bool>(CompleteWithResult(query, result.Value()));
            }
            return static_cast<bool>(CompleteWithFailure(query));
        }
        catch (...)
        {
            return static_cast<bool>(CompleteWithFailure(query));
        }
    }

    const auto result = BuildReferenceResult(query);
    if (!result)
    {
        return static_cast<bool>(CompleteWithFailure(query));
    }
    return static_cast<bool>(CompleteWithResult(query, result.Value()));
}

foundation::Result<PathResult> NavigationRuntime::BuildReferenceResult(const QueryRecord& query) const
{
    try
    {
        PathResult result{};
        result.handle = query.handle;
        result.state = PathQueryState::Completed;
        result.nav_revision = query.result.nav_revision;
        result.revision = NextRevisionValue(query.result.revision);
        result.points.reserve(3);

        const INavigationObstacleSource* obstacle_source = ObstacleSource();
        if (obstacle_source != nullptr)
        {
            for (const DynamicObstacle& obstacle : obstacle_source->ObstaclesForRegion(query.request.region))
            {
                if (obstacle.blocks_traversal)
                {
                    Vec3 midpoint{obstacle.bounds.max.x, query.request.start.y, obstacle.bounds.max.z};
                    if (!IsFinitePoint(midpoint))
                    {
                        return NavFailure<PathResult>("navigation.invalid_coordinate", "reference path generated a non-finite point");
                    }
                    result.points.push_back(query.request.start);
                    result.points.push_back(midpoint);
                    result.points.push_back(query.request.target);
                    return foundation::Result<PathResult>::Success(std::move(result));
                }
            }
        }

        const INavCostProvider* costs = CostProvider();
        const float cost = costs != nullptr ? costs->GetTraversalCost(NavCostQuery{query.request.region, {}, query.request.start}) : 1.0f;
        result.points.push_back(query.request.start);
        if (cost > 1.0f)
        {
            const Vec3 midpoint = SafeMidpoint(query.request.start, query.request.target);
            if (!IsFinitePoint(midpoint))
            {
                return NavFailure<PathResult>("navigation.invalid_coordinate", "reference path generated a non-finite point");
            }
            result.points.push_back(midpoint);
        }
        result.points.push_back(query.request.target);
        return foundation::Result<PathResult>::Success(std::move(result));
    }
    catch (const std::exception& error)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.provider_exception", "navigation provider threw during reference path build", error.what()));
    }
    catch (...)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.provider_exception", "navigation provider threw an unknown exception during reference path build"));
    }
}

foundation::Result<void> NavigationRuntime::CompleteWithResult(QueryRecord& query, PathResult result)
{
    if (auto revision = PreflightResultRevision(query); !revision)
    {
        return revision;
    }
    result.handle = query.handle;
    result.state = PathQueryState::Completed;
    result.nav_revision = query.result.nav_revision;
    result.revision = NextRevisionValue(query.result.revision);
    query.result = std::move(result);
    query.completed_at = std::chrono::steady_clock::now();
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationRuntime::CompleteWithFailure(QueryRecord& query)
{
    if (auto revision = PreflightResultRevision(query); !revision)
    {
        return revision;
    }
    query.result.state = PathQueryState::Failed;
    query.result.points.clear();
    query.result.revision = NextRevisionValue(query.result.revision);
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime::navigation
