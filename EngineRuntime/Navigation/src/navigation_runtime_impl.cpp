#include "navigation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <new>
#include <type_traits>
#include <utility>

namespace epidemic::runtime::navigation
{
static_assert(std::is_nothrow_move_constructible_v<PathResult>);
static_assert(std::is_nothrow_move_assignable_v<PathResult>);
static_assert(std::is_nothrow_move_constructible_v<foundation::Result<PathResult>>);
static_assert(std::is_nothrow_move_assignable_v<foundation::Result<PathResult>>);

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
    query->pending_backend_result.reset();
    query->pending_reference_plan.reset();
    query->result.revision = NextRevisionValue(query->result.revision);
    return foundation::Result<void>::Success();
}

std::size_t NavigationRuntime::Tick(RuntimeBudget budget)
{
    const auto purge_work_list = BuildPurgeWorkList();
    const auto query_work_list = BuildQueryWorkList();
    PurgeReleasedAndExpired(purge_work_list);

    const auto started_at = std::chrono::steady_clock::now();
    const std::size_t limit = BudgetLimit(budget, queries_.size());
    std::size_t transitioned = 0;

    for (const PathQueryId id : query_work_list)
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
    if (query->result.state == PathQueryState::Failed || query->result.state == PathQueryState::Cancelled ||
        query->result.state == PathQueryState::Stale)
    {
        return foundation::Result<PathQueryState>::Success(query->result.state);
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

    if (query->released || HasExpired(*query) || query->result.state == PathQueryState::Stale)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.stale_result", "path result is stale or released"));
    }
    if (query->result.state != PathQueryState::Completed)
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.query_not_completed", "path query has not completed"));
    }

    const auto changed = HasSourceRevisionChanged(*query);
    if (!changed)
    {
        return foundation::Result<PathResult>::Failure(changed.GetError());
    }
    if (changed.Value())
    {
        return foundation::Result<PathResult>::Failure(
            foundation::Error::Create("navigation.stale_result", "path result is stale or released"));
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
    if (fail_next_query_work_list_allocation_for_testing_)
    {
        fail_next_query_work_list_allocation_for_testing_ = false;
        throw std::bad_alloc{};
    }

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

std::vector<PathQueryId> NavigationRuntime::BuildPurgeWorkList() const
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
    return removed;
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

std::size_t NavigationRuntime::PathByteSize(std::size_t point_count) noexcept
{
    if (point_count > std::numeric_limits<std::size_t>::max() / sizeof(Vec3))
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return point_count * sizeof(Vec3);
}

bool NavigationRuntime::HasPathByteBudget(RuntimeBudget budget, std::size_t point_count) noexcept
{
    return !budget.HasByteBudget() || PathByteSize(point_count) <= budget.max_bytes;
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
        query.pending_backend_result.reset();
        query.pending_reference_plan.reset();
        query.result.revision = NextRevisionValue(query.result.revision);
    }
}

void NavigationRuntime::PurgeReleasedAndExpired(const std::vector<PathQueryId>& removed) noexcept
{
    for (PathQueryId id : removed)
    {
        queries_.erase(id);
    }
}

bool NavigationRuntime::CompleteQuery(QueryRecord& query, RuntimeBudget budget)
{
    if (auto revision = PreflightResultRevision(query); !revision)
    {
        return false;
    }

    if (const INavigationBackend* backend = Backend(); backend != nullptr)
    {
        if (!query.pending_backend_result.has_value())
        {
            foundation::Result<PathResult> backend_result = NavFailure<PathResult>(
                "navigation.backend_failed", "navigation backend did not produce a result");
            try
            {
                backend_result = backend->BuildPath(query.request, NavigationRevision{query.result.nav_revision});
            }
            catch (const std::exception&)
            {
                return static_cast<bool>(CompleteWithFailure(query));
            }
            catch (...)
            {
                return static_cast<bool>(CompleteWithFailure(query));
            }

            if (!backend_result)
            {
                return static_cast<bool>(CompleteWithFailure(query));
            }
            if (!IsValidPathResult(query.request, backend_result.Value(), NavigationRevision{query.result.nav_revision}))
            {
                return static_cast<bool>(CompleteWithFailure(query));
            }

            query.pending_backend_result.emplace(std::move(backend_result.Value()));
            if (fail_after_backend_success_for_testing_)
            {
                fail_after_backend_success_for_testing_ = false;
                throw std::bad_alloc{};
            }
        }

        if (!HasPathByteBudget(budget, query.pending_backend_result->points.size()))
        {
            return false;
        }

        const auto completed = CompleteWithResult(query, std::move(query.pending_backend_result.value()));
        if (completed)
        {
            query.pending_backend_result.reset();
        }
        return static_cast<bool>(completed);
    }

    if (!query.pending_reference_plan.has_value())
    {
        const auto plan = PrepareReferencePathPlan(query);
        if (!plan)
        {
            return static_cast<bool>(CompleteWithFailure(query));
        }
        query.pending_reference_plan.emplace(plan.Value());
    }

    if (!HasPathByteBudget(budget, query.pending_reference_plan->point_count))
    {
        return false;
    }

    auto result = BuildReferenceResult(query, query.pending_reference_plan.value());
    if (!result)
    {
        return static_cast<bool>(CompleteWithFailure(query));
    }

    PathResult staged = std::move(result.Value());
    const auto completed = CompleteWithResult(query, std::move(staged));
    if (completed)
    {
        query.pending_reference_plan.reset();
    }
    return static_cast<bool>(completed);
}

foundation::Result<NavigationRuntime::ReferencePathPlan> NavigationRuntime::PrepareReferencePathPlan(const QueryRecord& query) const
{
    ReferencePathPlan plan{};

    if (const INavigationObstacleSource* obstacle_source = ObstacleSource(); obstacle_source != nullptr)
    {
        std::span<const DynamicObstacle> obstacles;
        try
        {
            obstacles = obstacle_source->ObstaclesForRegion(query.request.region);
        }
        catch (const std::exception& error)
        {
            return foundation::Result<ReferencePathPlan>::Failure(
                foundation::Error::Create("navigation.provider_exception", "navigation obstacle source threw", error.what()));
        }
        catch (...)
        {
            return foundation::Result<ReferencePathPlan>::Failure(
                foundation::Error::Create("navigation.provider_exception", "navigation obstacle source threw an unknown exception"));
        }

        for (const DynamicObstacle& obstacle : obstacles)
        {
            if (obstacle.blocks_traversal)
            {
                const Vec3 midpoint{obstacle.bounds.max.x, query.request.start.y, obstacle.bounds.max.z};
                if (!IsFinitePoint(midpoint))
                {
                    return NavFailure<ReferencePathPlan>(
                        "navigation.invalid_coordinate", "reference path generated a non-finite obstacle detour point");
                }
                plan.point_count = 3;
                plan.midpoint = midpoint;
                return foundation::Result<ReferencePathPlan>::Success(plan);
            }
        }
    }

    float cost = 1.0f;
    if (const INavCostProvider* costs = CostProvider(); costs != nullptr)
    {
        try
        {
            cost = costs->GetTraversalCost(NavCostQuery{query.request.region, {}, query.request.start});
        }
        catch (const std::exception& error)
        {
            return foundation::Result<ReferencePathPlan>::Failure(
                foundation::Error::Create("navigation.provider_exception", "navigation cost provider threw", error.what()));
        }
        catch (...)
        {
            return foundation::Result<ReferencePathPlan>::Failure(
                foundation::Error::Create("navigation.provider_exception", "navigation cost provider threw an unknown exception"));
        }
    }

    if (!std::isfinite(cost) || cost < 0.0f)
    {
        return NavFailure<ReferencePathPlan>(
            "navigation.invalid_provider_data", "navigation traversal cost must be finite and non-negative");
    }
    if (cost > 1.0f)
    {
        const Vec3 midpoint = SafeMidpoint(query.request.start, query.request.target);
        if (!IsFinitePoint(midpoint))
        {
            return NavFailure<ReferencePathPlan>(
                "navigation.invalid_coordinate", "reference path generated a non-finite cost midpoint");
        }
        plan.point_count = 3;
        plan.midpoint = midpoint;
    }

    return foundation::Result<ReferencePathPlan>::Success(plan);
}

foundation::Result<PathResult> NavigationRuntime::BuildReferenceResult(
    const QueryRecord& query, const ReferencePathPlan& plan) const
{
    if (fail_next_reference_result_allocation_for_testing_)
    {
        fail_next_reference_result_allocation_for_testing_ = false;
        throw std::bad_alloc{};
    }

    PathResult result{};
    result.handle = query.handle;
    result.state = PathQueryState::Completed;
    result.nav_revision = query.result.nav_revision;
    result.revision = NextRevisionValue(query.result.revision);
    result.points.reserve(plan.point_count);
    result.points.push_back(query.request.start);
    if (plan.midpoint.has_value())
    {
        result.points.push_back(plan.midpoint.value());
    }
    result.points.push_back(query.request.target);
    return foundation::Result<PathResult>::Success(std::move(result));
}

foundation::Result<void> NavigationRuntime::CompleteWithResult(QueryRecord& query, PathResult&& result)
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
    query.pending_backend_result.reset();
    query.pending_reference_plan.reset();
    query.result.revision = NextRevisionValue(query.result.revision);
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime::navigation
