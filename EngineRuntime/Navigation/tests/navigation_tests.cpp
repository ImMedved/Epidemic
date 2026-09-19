#include "navigation_runtime_impl.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>


namespace epidemic::runtime::navigation
{
struct NavigationRuntimeTestAccess
{
    static void SetNextIdentity(NavigationRuntime& runtime, std::uint64_t id, std::uint32_t generation) noexcept
    {
        runtime.next_query_value_ = id;
        runtime.next_generation_ = generation;
    }

    static void SetNavigationRevision(NavigationRuntime& runtime, std::uint64_t revision) noexcept
    {
        runtime.nav_revision_ = revision;
    }

    static void SetResultRevision(NavigationRuntime& runtime, PathQueryHandle handle, std::uint64_t revision) noexcept
    {
        if (auto* query = runtime.FindQuery(handle.id); query != nullptr && query->handle.generation == handle.generation)
        {
            query->result.revision = revision;
        }
    }

    static void FailNextQueryWorkListAllocation(NavigationRuntime& runtime) noexcept
    {
        runtime.fail_next_query_work_list_allocation_for_testing_ = true;
    }

    static void FailAfterBackendSuccess(NavigationRuntime& runtime) noexcept
    {
        runtime.fail_after_backend_success_for_testing_ = true;
    }

    static void FailNextReferenceResultAllocation(NavigationRuntime& runtime) noexcept
    {
        runtime.fail_next_reference_result_allocation_for_testing_ = true;
    }

    static bool CanTileTransition(NavTileState from, NavTileState to) noexcept
    {
        return NavigationRuntime::CanTransition(from, to);
    }
};
} // namespace epidemic::runtime::navigation

using epidemic::runtime::RegionId;
using epidemic::runtime::RuntimeBudget;
using epidemic::runtime::Vec3;
using epidemic::runtime::navigation::DynamicObstacle;
using epidemic::runtime::navigation::DynamicObstacleId;
using epidemic::runtime::navigation::INavigationBackend;
using epidemic::runtime::navigation::INavigationDataSource;
using epidemic::runtime::navigation::INavigationObstacleSource;
using epidemic::runtime::navigation::INavCostProvider;
using epidemic::runtime::navigation::CreateNavigationServices;
using epidemic::runtime::navigation::CreateMockNavigationServices;
using epidemic::runtime::navigation::NavCostQuery;
using epidemic::runtime::navigation::NavTileId;
using epidemic::runtime::navigation::NavTileState;
using epidemic::runtime::navigation::NavigationRevision;
using epidemic::runtime::navigation::NavigationDependencies;
using epidemic::runtime::navigation::NavigationOptions;
using epidemic::runtime::navigation::NavigationRuntime;
using epidemic::runtime::navigation::NavigationRuntimeTestAccess;
using epidemic::runtime::navigation::PathResult;
using epidemic::runtime::navigation::PathQueryState;
using epidemic::runtime::navigation::PathRequest;

namespace
{
struct FixedCostProvider final : INavCostProvider
{
    float GetTraversalCost(const NavCostQuery& query) const override
    {
        return query.region.IsValid() ? 2.0f : 1.0f;
    }
};

struct FixedObstacleSource final : INavigationObstacleSource
{
    std::vector<DynamicObstacle> obstacles;

    std::span<const DynamicObstacle> ObstaclesForRegion(RegionId region) const override
    {
        if (!region.IsValid())
        {
            return {};
        }

        return obstacles;
    }
};

struct FixedNavigationBackend final : INavigationBackend
{
    bool fail = false;

    epidemic::foundation::Result<PathResult> BuildPath(const PathRequest& request, NavigationRevision revision) const override
    {
        if (fail)
        {
            return epidemic::foundation::Result<PathResult>::Failure(
                epidemic::foundation::Error::Create("navigation.backend_failed", "backend failed"));
        }

        PathResult result{};
        result.state = PathQueryState::Completed;
        result.points = {request.start, request.target};
        result.nav_revision = revision.value;
        result.revision = 1;
        return epidemic::foundation::Result<PathResult>::Success(result);
    }
};


struct ThrowingNavigationDataSource final : INavigationDataSource
{
    NavigationRevision CurrentRevision(RegionId) const override
    {
        throw std::runtime_error("revision boom");
    }
};

struct ThrowingCostProvider final : INavCostProvider
{
    float GetTraversalCost(const NavCostQuery&) const override
    {
        throw std::runtime_error("cost boom");
    }
};

struct FixedNavigationDataSource final : INavigationDataSource
{
    NavigationRevision revision{99};

    NavigationRevision CurrentRevision(RegionId) const override
    {
        return revision;
    }
};

struct CountingCostProvider final : INavCostProvider
{
    mutable std::size_t calls = 0;
    float cost = 2.0f;
    bool should_throw = false;

    float GetTraversalCost(const NavCostQuery&) const override
    {
        ++calls;
        if (should_throw)
        {
            throw std::runtime_error("cost provider boom");
        }
        return cost;
    }
};

struct CountingObstacleSource final : INavigationObstacleSource
{
    mutable std::size_t calls = 0;
    bool should_throw = false;
    std::vector<DynamicObstacle> obstacles;

    std::span<const DynamicObstacle> ObstaclesForRegion(RegionId) const override
    {
        ++calls;
        if (should_throw)
        {
            throw std::runtime_error("obstacle source boom");
        }
        return obstacles;
    }
};

struct CountingNavigationBackend final : INavigationBackend
{
    enum class Mode
    {
        Success,
        Failure,
        Throw,
        WrongState,
        WrongRevision,
        WrongStart,
        WrongTarget,
        NonFinite,
        TooShort
    };

    mutable std::size_t calls = 0;
    Mode mode = Mode::Success;
    std::size_t point_count = 2;

    epidemic::foundation::Result<PathResult> BuildPath(const PathRequest& request, NavigationRevision revision) const override
    {
        ++calls;
        if (mode == Mode::Throw)
        {
            throw std::runtime_error("backend boom");
        }
        if (mode == Mode::Failure)
        {
            return epidemic::foundation::Result<PathResult>::Failure(
                epidemic::foundation::Error::Create("navigation.backend_failed", "backend failed"));
        }

        PathResult result{};
        result.state = mode == Mode::WrongState ? PathQueryState::PartiallyComplete : PathQueryState::Completed;
        result.nav_revision = mode == Mode::WrongRevision ? revision.value + 1 : revision.value;
        result.revision = 1;

        if (mode != Mode::TooShort)
        {
            result.points.reserve(point_count);
            result.points.push_back(mode == Mode::WrongStart ? Vec3{1.0f, 0.0f, 0.0f} : request.start);
            while (result.points.size() + 1 < point_count)
            {
                const float x = static_cast<float>(result.points.size());
                result.points.push_back(Vec3{x, 0.0f, x});
            }
            if (point_count > 1)
            {
                result.points.push_back(mode == Mode::WrongTarget ? Vec3{99.0f, 0.0f, 0.0f} : request.target);
            }
        }
        else
        {
            result.points.push_back(request.start);
        }

        if (mode == Mode::NonFinite && !result.points.empty())
        {
            result.points[result.points.size() / 2].x = std::numeric_limits<float>::quiet_NaN();
        }
        return epidemic::foundation::Result<PathResult>::Success(std::move(result));
    }
};

struct ThrowAfterFirstRevisionSource final : INavigationDataSource
{
    mutable std::size_t calls = 0;
    NavigationRevision revision{7};

    NavigationRevision CurrentRevision(RegionId) const override
    {
        ++calls;
        if (calls > 1)
        {
            throw std::runtime_error("revision later boom");
        }
        return revision;
    }
};

PathRequest MakeRequest()
{
    return PathRequest{Vec3{0.0f, 0.0f, 0.0f}, Vec3{10.0f, 0.0f, 0.0f}, RegionId{7}};
}

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
}

template <typename RuntimeT>
bool ExpectPathState(const RuntimeT& runtime, epidemic::runtime::navigation::PathQueryHandle handle, PathQueryState expected, std::string_view message)
{
    const auto actual = runtime.GetPathState(handle);
    return Expect(actual.HasValue() && actual.Value() == expected, message);
}

bool AdvanceToPartiallyComplete(NavigationRuntime& runtime, epidemic::runtime::navigation::PathQueryHandle handle)
{
    return Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "query should enter running") &&
           Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "query should enter partial state") &&
           ExpectPathState(runtime, handle, PathQueryState::PartiallyComplete, "query should be partially complete");
}

bool TestRequestPathCompletesThroughBudgetedStates()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const auto query = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue(), "path request should succeed"))
    {
        return false;
    }

    const auto handle = query.Value();
    bool ok = ExpectPathState(runtime, handle, PathQueryState::Pending, "new query should start pending");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "first tick should advance one query");
    ok &= ExpectPathState(runtime, handle, PathQueryState::Running, "first tick should move query to running");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "second tick should partially complete query");
    ok &= ExpectPathState(runtime, handle, PathQueryState::PartiallyComplete, "second tick should partially complete query");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "third tick should complete query");
    ok &= ExpectPathState(runtime, handle, PathQueryState::Completed, "third tick should complete query");
    const auto result = runtime.GetPathResult(handle);
    ok &= Expect(result.HasValue(), "completed query should have result");
    ok &= Expect(result.HasValue() && result.Value().points.size() == 2, "default mock path should contain start and target");
    ok &= Expect(result.HasValue() && result.Value().revision == 4, "completed query should have four revisions");
    return ok;
}

bool TestPathBudgetOrderIsDeterministic()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const auto first = runtime.RequestPathHandle(MakeRequest());
    const auto second = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(first.HasValue() && second.HasValue(), "path requests should succeed"))
    {
        return false;
    }

    bool ok = Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "budget should advance one query");
    ok &= ExpectPathState(runtime, first.Value(), PathQueryState::Running, "first query should advance first");
    ok &= ExpectPathState(runtime, second.Value(), PathQueryState::Pending, "second query should wait");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "budget should advance next query");
    ok &= ExpectPathState(runtime, first.Value(), PathQueryState::PartiallyComplete, "first query should partially complete before second starts");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "budget should complete first query");
    ok &= ExpectPathState(runtime, first.Value(), PathQueryState::Completed, "first query should complete before second starts");
    ok &= ExpectPathState(runtime, second.Value(), PathQueryState::Pending, "second query should still wait");
    return ok;
}

bool TestCancelledQueryDoesNotProducePath()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const auto query = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue(), "path request should succeed before cancellation"))
    {
        return false;
    }

    const auto handle = query.Value();
    bool ok = Expect(runtime.CancelPath(handle).HasValue(), "pending query should cancel");
    ok &= ExpectPathState(runtime, handle, PathQueryState::Cancelled, "cancelled query should report cancelled");
    ok &= Expect(!runtime.GetPathResult(handle).HasValue(), "cancelled query should not expose path result");
    ok &= Expect(runtime.Tick(RuntimeBudget{}) == 0, "cancelled query should not consume tick work");
    return ok;
}

bool TestCancelPartiallyCompletedQuery()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const auto query = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue(), "path request should succeed before partial cancellation"))
    {
        return false;
    }

    (void)runtime.Tick(RuntimeBudget{.max_items = 1});
    (void)runtime.Tick(RuntimeBudget{.max_items = 1});
    bool ok = ExpectPathState(runtime, query.Value(), PathQueryState::PartiallyComplete, "query should be partially complete before cancel");
    ok &= Expect(runtime.CancelPath(query.Value()).HasValue(), "partially complete query should cancel");
    ok &= ExpectPathState(runtime, query.Value(), PathQueryState::Cancelled, "cancelled partial query should report cancelled");
    ok &= Expect(!runtime.GetPathResult(query.Value()).HasValue(), "cancelled partial query should not expose path");
    return ok;
}

bool TestTileDirtyRebuildStates()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const NavTileId tile{42};
    const NavTileId loading_tile{43};
    bool ok = Expect(runtime.GetTileState(tile) == NavTileState::Missing, "unknown tile should be missing");
    ok &= Expect(runtime.RegisterTile(tile, NavTileState::Ready).HasValue(), "valid tile should register");
    ok &= Expect(runtime.RegisterTile(loading_tile, NavTileState::Loading).HasValue(), "loading tile should register");
    ok &= Expect(!runtime.MarkTileDirty(loading_tile).HasValue(), "loading tile should reject dirty transition");
    ok &= Expect(runtime.MarkTileDirty(tile).HasValue(), "registered tile should become dirty");
    ok &= Expect(runtime.GetTileState(tile) == NavTileState::Dirty, "tile should be dirty after mark");
    ok &= Expect(runtime.RebuildDirtyTiles(RuntimeBudget{.max_items = 1}) == 1, "first rebuild tick should transition dirty tile");
    ok &= Expect(runtime.GetTileState(tile) == NavTileState::Rebuilding, "tile should be rebuilding after first rebuild tick");
    ok &= Expect(runtime.RebuildDirtyTiles(RuntimeBudget{.max_items = 1}) == 1, "second rebuild tick should transition rebuilding tile");
    ok &= Expect(runtime.GetTileState(tile) == NavTileState::Ready, "tile should return to ready after rebuild");
    return ok;
}

bool TestCostAndObstacleProjectionsStayExternal()
{
    auto costs = std::make_shared<FixedCostProvider>();
    auto obstacles = std::make_shared<FixedObstacleSource>();
    obstacles->obstacles.push_back(DynamicObstacle{DynamicObstacleId{5}, RegionId{7}, {}, true, 1.0f});
    NavigationRuntime runtime{NavigationOptions{}, NavigationDependencies{{}, {}, costs, obstacles}};

    const auto query = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue(), "path request with projection sources should succeed"))
    {
        return false;
    }

    (void)runtime.Tick(RuntimeBudget{});
    (void)runtime.Tick(RuntimeBudget{});
    (void)runtime.Tick(RuntimeBudget{});
    const auto result = runtime.GetPathResult(query.Value());
    bool ok = Expect(result.HasValue(), "projection-backed query should complete");
    ok &= Expect(result.HasValue() && result.Value().points.size() == 3, "blocking obstacle should add deterministic detour point");
    return ok;
}

bool TestInvalidInputsReturnFailures()
{
    NavigationRuntime runtime{NavigationOptions{}};
    bool ok = Expect(!runtime.RequestPathHandle(PathRequest{}).HasValue(), "invalid region should fail path request");
    ok &= Expect(!runtime.RegisterTile(NavTileId{}, NavTileState::Ready).HasValue(), "invalid tile id should fail registration");
    ok &= Expect(!runtime.MarkTileDirty(NavTileId{99}).HasValue(), "unknown tile should fail dirty mark");
    return ok;
}

bool TestFactoryBackendSelectionAndFailure()
{
    auto backend = std::make_shared<FixedNavigationBackend>();
    auto data_source = std::make_shared<FixedNavigationDataSource>();

    const auto production = CreateNavigationServices(
        NavigationOptions{.enable_mock_queries = false},
        NavigationDependencies{backend, data_source, {}, {}});
    if (!Expect(production.HasValue(), "real backend should work with mock disabled"))
    {
        return false;
    }

    const auto query = production.Value().runtime->RequestPathHandle(MakeRequest());
    bool ok = Expect(query.HasValue(), "production backend query should be accepted");
    ok &= Expect(production.Value().runtime->Tick(RuntimeBudget{}) == 1, "production query should enter running");
    ok &= Expect(production.Value().runtime->Tick(RuntimeBudget{}) == 1, "production query should partially complete");
    ok &= Expect(production.Value().runtime->Tick(RuntimeBudget{}) == 1, "production backend query should complete");
    ok &= ExpectPathState(*production.Value().runtime, query.Value(), PathQueryState::Completed, "production backend query should complete");

    auto failing_backend = std::make_shared<FixedNavigationBackend>();
    failing_backend->fail = true;
    const auto failing = CreateNavigationServices(
        NavigationOptions{.enable_mock_queries = true},
        NavigationDependencies{failing_backend, data_source, {}, {}});
    const auto failing_query = failing.Value().runtime->RequestPathHandle(MakeRequest());
    (void)failing.Value().runtime->Tick(RuntimeBudget{});
    (void)failing.Value().runtime->Tick(RuntimeBudget{});
    (void)failing.Value().runtime->Tick(RuntimeBudget{});
    ok &= ExpectPathState(*failing.Value().runtime, failing_query.Value(), PathQueryState::Failed,
                 "backend failure should not fall back to reference mock");

    const auto missing = CreateNavigationServices(NavigationOptions{.enable_mock_queries = false}, {});
    ok &= Expect(!missing.HasValue() && missing.GetError().HasCode("navigation.backend_missing"),
                 "backend missing with mock disabled should fail factory");

    const auto reference = CreateNavigationServices(NavigationOptions{.enable_mock_queries = true}, {});
    ok &= Expect(reference.HasValue(), "reference mock should be explicit through mock-enabled profile");
    return ok;
}

bool TestHandleReleaseTtlAndStaleRevision()
{
    auto backend = std::make_shared<FixedNavigationBackend>();
    auto data_source = std::make_shared<FixedNavigationDataSource>();
    NavigationRuntime runtime{NavigationOptions{}, NavigationDependencies{backend, data_source, {}, {}}};

    auto request = MakeRequest();
    request.source_revision = 99;
    const auto handle = runtime.RequestPathHandle(request);
    if (!Expect(handle.HasValue(), "path handle request should succeed"))
    {
        return false;
    }

    bool ok = Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "handle query should enter running");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "handle query should partially complete");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "handle query should complete");
    const auto result = runtime.GetPathResult(handle.Value());
    ok &= Expect(result.HasValue(), "handle query should expose completed result");
    ok &= Expect(result.HasValue() && result.Value().nav_revision == 99, "data source revision should be captured");
    data_source->revision = NavigationRevision{100};
    ok &= ExpectPathState(runtime, handle.Value(), PathQueryState::Stale, "changed nav revision should mark query stale");
    ok &= Expect(!runtime.GetPathResult(handle.Value()).HasValue(), "stale revision should hide result");

    auto ttl_source = std::make_shared<FixedNavigationDataSource>();
    NavigationRuntime ttl_runtime{NavigationOptions{}, NavigationDependencies{backend, ttl_source, {}, {}}};
    auto ttl_request = MakeRequest();
    ttl_request.result_ttl = std::chrono::microseconds{1};
    const auto ttl_handle = ttl_runtime.RequestPathHandle(ttl_request);
    (void)ttl_runtime.Tick(RuntimeBudget{});
    (void)ttl_runtime.Tick(RuntimeBudget{});
    (void)ttl_runtime.Tick(RuntimeBudget{});
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
    ok &= ExpectPathState(ttl_runtime, ttl_handle.Value(), PathQueryState::Stale, "expired TTL should mark result stale");

    ok &= Expect(runtime.ReleasePathResult(handle.Value()).HasValue(), "result release should succeed");
    ok &= Expect(!runtime.GetPathResult(handle.Value()).HasValue(), "released result should be unavailable");

    const auto services = CreateMockNavigationServices();
    ok &= Expect(services.runtime != nullptr && services.tiles != nullptr, "mock navigation services should be populated");
    return ok;
}


bool TestSourceRevisionZeroBecomesStaleAfterAdvance()
{
    auto data_source = std::make_shared<FixedNavigationDataSource>();
    data_source->revision = NavigationRevision{0};
    NavigationRuntime runtime{NavigationOptions{}, NavigationDependencies{{}, data_source, {}, {}}};
    const auto query = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue(), "zero-revision source request should succeed"))
    {
        return false;
    }
    data_source->revision = NavigationRevision{1};
    return ExpectPathState(runtime, query.Value(), PathQueryState::Stale, "zero source revision must not be absence sentinel");
}

bool TestInvalidNumericPathRequestsAreRejected()
{
    NavigationRuntime runtime{NavigationOptions{}};
    auto nan = MakeRequest();
    nan.start.x = std::numeric_limits<float>::quiet_NaN();
    auto inf = MakeRequest();
    inf.target.z = std::numeric_limits<float>::infinity();
    auto negative_ttl = MakeRequest();
    negative_ttl.result_ttl = std::chrono::microseconds{-1};

    const auto nan_result = runtime.RequestPathHandle(nan);
    const auto inf_result = runtime.RequestPathHandle(inf);
    const auto ttl_result = runtime.RequestPathHandle(negative_ttl);
    return Expect(!nan_result && nan_result.GetError().HasCode("navigation.invalid_coordinate"), "NaN must be rejected") &&
           Expect(!inf_result && inf_result.GetError().HasCode("navigation.invalid_coordinate"), "Inf must be rejected") &&
           Expect(!ttl_result && ttl_result.GetError().HasCode("navigation.invalid_ttl"), "negative TTL must be rejected");
}

bool TestProviderExceptionsBecomeFailures()
{
    auto throwing_source = std::make_shared<ThrowingNavigationDataSource>();
    NavigationRuntime source_runtime{NavigationOptions{}, NavigationDependencies{{}, throwing_source, {}, {}}};
    const auto source_query = source_runtime.RequestPathHandle(MakeRequest());
    if (!Expect(!source_query && source_query.GetError().HasCode("navigation.provider_exception"),
                "data source exception should be a Result failure"))
    {
        return false;
    }

    auto throwing_cost = std::make_shared<ThrowingCostProvider>();
    NavigationRuntime cost_runtime{NavigationOptions{}, NavigationDependencies{{}, {}, throwing_cost, {}}};
    const auto cost_query = cost_runtime.RequestPathHandle(MakeRequest());
    if (!Expect(cost_query.HasValue(), "cost-provider query should be accepted before provider use"))
    {
        return false;
    }
    (void)cost_runtime.Tick(RuntimeBudget{});
    (void)cost_runtime.Tick(RuntimeBudget{});
    (void)cost_runtime.Tick(RuntimeBudget{});
    return ExpectPathState(cost_runtime, cost_query.Value(), PathQueryState::Failed,
                           "provider exception should fail the individual query");
}

bool TestRepeatedDirtyIsNoOp()
{
    NavigationRuntime runtime{NavigationOptions{}};
    bool ok = Expect(runtime.RegisterTile(NavTileId{1}, NavTileState::Ready).HasValue(), "tile should register");
    ok &= Expect(runtime.MarkTileDirty(NavTileId{1}).HasValue(), "first dirty should succeed");
    ok &= Expect(runtime.MarkTileDirty(NavTileId{1}).HasValue(), "second dirty should be idempotent no-op");
    ok &= Expect(runtime.GetTileState(NavTileId{1}) == NavTileState::Dirty, "tile should remain dirty");
    return ok;
}

bool TestByteBudgetLimitsCompletion()
{
    auto obstacle_source = std::make_shared<FixedObstacleSource>();
    obstacle_source->obstacles.push_back(DynamicObstacle{DynamicObstacleId{5}, RegionId{7}, {}, true, 1.0f});
    NavigationRuntime runtime{NavigationOptions{}, NavigationDependencies{{}, {}, {}, obstacle_source}};
    const auto query = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue(), "budget query should be accepted"))
    {
        return false;
    }

    (void)runtime.Tick(RuntimeBudget{});
    (void)runtime.Tick(RuntimeBudget{});
    bool ok = ExpectPathState(runtime, query.Value(), PathQueryState::PartiallyComplete, "query should wait at partial state");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3)}) == 0, "small byte budget should not complete path");
    ok &= ExpectPathState(runtime, query.Value(), PathQueryState::PartiallyComplete, "query should remain partial under byte budget");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 3}) == 1, "sufficient byte budget should complete path");
    ok &= ExpectPathState(runtime, query.Value(), PathQueryState::Completed, "query should complete with sufficient byte budget");
    return ok;
}


bool TestTileRegistrationTransitionBudgetAndRevisionAudit()
{
    NavigationRuntime runtime{NavigationOptions{}};
    bool ok = true;

    const NavTileState states[] = {
        NavTileState::Missing, NavTileState::Queued, NavTileState::Loading, NavTileState::Ready,
        NavTileState::Dirty, NavTileState::Rebuilding, NavTileState::Failed, NavTileState::Unloaded};
    const auto expected_transition = [](NavTileState from, NavTileState to) noexcept {
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
    };
    for (const auto from : states)
    {
        for (const auto to : states)
        {
            ok &= Expect(NavigationRuntimeTestAccess::CanTileTransition(from, to) == expected_transition(from, to),
                         "tile transition matrix must match the frozen contract");
        }
    }
    ok &= Expect(!runtime.RegisterTile(NavTileId{}, NavTileState::Ready).HasValue(), "zero tile id must be rejected");
    ok &= Expect(!runtime.RegisterTile(NavTileId{1}, NavTileState::Missing).HasValue(), "Missing is not a valid initial state");
    ok &= Expect(!runtime.RegisterTile(NavTileId{2}, NavTileState::Dirty).HasValue(), "Dirty is not a valid initial state");
    ok &= Expect(!runtime.RegisterTile(NavTileId{3}, static_cast<NavTileState>(999)).HasValue(), "unknown tile enum must be rejected");
    ok &= Expect(runtime.RegisterTile(NavTileId{10}, NavTileState::Queued).HasValue(), "Queued should register");
    ok &= Expect(runtime.RegisterTile(NavTileId{11}, NavTileState::Loading).HasValue(), "Loading should register");
    ok &= Expect(runtime.RegisterTile(NavTileId{12}, NavTileState::Ready).HasValue(), "Ready should register");
    ok &= Expect(runtime.RegisterTile(NavTileId{13}, NavTileState::Unloaded).HasValue(), "Unloaded should register");
    ok &= Expect(!runtime.RegisterTile(NavTileId{12}, NavTileState::Ready).HasValue(), "duplicate tile must be rejected");
    ok &= Expect(!runtime.MarkTileDirty(NavTileId{10}).HasValue(), "Queued cannot become Dirty through public registry API");
    ok &= Expect(!runtime.MarkTileDirty(NavTileId{11}).HasValue(), "Loading cannot become Dirty through public registry API");
    ok &= Expect(!runtime.MarkTileDirty(NavTileId{13}).HasValue(), "Unloaded cannot become Dirty through public registry API");
    ok &= Expect(runtime.MarkTileDirty(NavTileId{12}).HasValue(), "Ready can become Dirty");
    ok &= Expect(runtime.MarkTileDirty(NavTileId{12}).HasValue(), "repeated Dirty must be a no-op");

    NavigationRuntime ordered{NavigationOptions{}};
    ok &= Expect(ordered.RegisterTile(NavTileId{20}, NavTileState::Ready).HasValue(), "tile 20 should register");
    ok &= Expect(ordered.RegisterTile(NavTileId{5}, NavTileState::Ready).HasValue(), "tile 5 should register");
    ok &= Expect(ordered.MarkTileDirty(NavTileId{20}).HasValue(), "tile 20 should dirty");
    ok &= Expect(ordered.MarkTileDirty(NavTileId{5}).HasValue(), "tile 5 should dirty");
    ok &= Expect(ordered.RebuildDirtyTiles(RuntimeBudget{.max_items = 1}) == 1, "item budget should limit rebuild transitions");
    ok &= Expect(ordered.GetTileState(NavTileId{5}) == NavTileState::Rebuilding,
                 "dirty rebuild order must be deterministic by tile id");
    ok &= Expect(ordered.GetTileState(NavTileId{20}) == NavTileState::Dirty,
                 "higher tile id must wait behind lower tile id");
    ok &= Expect(ordered.RebuildDirtyTiles(RuntimeBudget{.max_bytes = 1}) == 2,
                 "byte-only budget does not limit zero-payload tile state transitions");
    ok &= Expect(ordered.GetTileState(NavTileId{5}) == NavTileState::Ready &&
                 ordered.GetTileState(NavTileId{20}) == NavTileState::Rebuilding,
                 "byte-only rebuild should follow deterministic transition order");

    NavigationRuntime register_overflow{NavigationOptions{}};
    NavigationRuntimeTestAccess::SetNavigationRevision(register_overflow, std::numeric_limits<std::uint64_t>::max());
    const auto register_failed = register_overflow.RegisterTile(NavTileId{1}, NavTileState::Ready);
    ok &= Expect(!register_failed && register_failed.GetError().HasCode("navigation.revision_exhausted"),
                 "tile registration must reject revision exhaustion before publication");
    ok &= Expect(register_overflow.GetTileState(NavTileId{1}) == NavTileState::Missing,
                 "failed exhausted registration must leave tile absent");

    NavigationRuntime dirty_overflow{NavigationOptions{}};
    ok &= Expect(dirty_overflow.RegisterTile(NavTileId{1}, NavTileState::Ready).HasValue(), "overflow dirty tile should register");
    NavigationRuntimeTestAccess::SetNavigationRevision(dirty_overflow, std::numeric_limits<std::uint64_t>::max());
    const auto dirty_failed = dirty_overflow.MarkTileDirty(NavTileId{1});
    ok &= Expect(!dirty_failed && dirty_failed.GetError().HasCode("navigation.revision_exhausted"),
                 "dirty transition must reject revision exhaustion");
    ok &= Expect(dirty_overflow.GetTileState(NavTileId{1}) == NavTileState::Ready,
                 "failed dirty transition must preserve pre-state");

    NavigationRuntime rebuild_overflow{NavigationOptions{}};
    ok &= Expect(rebuild_overflow.RegisterTile(NavTileId{1}, NavTileState::Ready).HasValue(), "overflow rebuild tile should register");
    ok &= Expect(rebuild_overflow.MarkTileDirty(NavTileId{1}).HasValue(), "overflow rebuild tile should dirty");
    NavigationRuntimeTestAccess::SetNavigationRevision(rebuild_overflow, std::numeric_limits<std::uint64_t>::max());
    ok &= Expect(rebuild_overflow.RebuildDirtyTiles(RuntimeBudget{}) == 0, "rebuild must stop before exhausted revision mutation");
    ok &= Expect(rebuild_overflow.GetTileState(NavTileId{1}) == NavTileState::Dirty,
                 "revision exhaustion must preserve dirty tile state");
    return ok;
}

bool TestQueryIdentityGenerationAndResultRevisionExhaustion()
{
    bool ok = true;
    NavigationRuntime id_zero{NavigationOptions{}};
    NavigationRuntimeTestAccess::SetNextIdentity(id_zero, 0, 1);
    const auto id_zero_result = id_zero.RequestPathHandle(MakeRequest());
    ok &= Expect(!id_zero_result && id_zero_result.GetError().HasCode("navigation.query_id_exhausted"),
                 "zero next query id must be exhausted");

    NavigationRuntime generation_zero{NavigationOptions{}};
    NavigationRuntimeTestAccess::SetNextIdentity(generation_zero, 1, 0);
    const auto generation_zero_result = generation_zero.RequestPathHandle(MakeRequest());
    ok &= Expect(!generation_zero_result && generation_zero_result.GetError().HasCode("navigation.query_generation_exhausted"),
                 "zero next generation must be exhausted");

    NavigationRuntime id_max{NavigationOptions{}};
    NavigationRuntimeTestAccess::SetNextIdentity(id_max, std::numeric_limits<std::uint64_t>::max(), 1);
    const auto last_id = id_max.RequestPathHandle(MakeRequest());
    ok &= Expect(last_id.HasValue() && last_id.Value().id.value == std::numeric_limits<std::uint64_t>::max(),
                 "maximum non-zero query id must be allocatable once");
    const auto exhausted_id = id_max.RequestPathHandle(MakeRequest());
    ok &= Expect(!exhausted_id && exhausted_id.GetError().HasCode("navigation.query_id_exhausted"),
                 "query id must become exhausted after maximum value");

    NavigationRuntime generation_max{NavigationOptions{}};
    NavigationRuntimeTestAccess::SetNextIdentity(generation_max, 1, std::numeric_limits<std::uint32_t>::max());
    const auto last_generation = generation_max.RequestPathHandle(MakeRequest());
    ok &= Expect(last_generation.HasValue() && last_generation.Value().generation == std::numeric_limits<std::uint32_t>::max(),
                 "maximum non-zero generation must be allocatable once");
    const auto exhausted_generation = generation_max.RequestPathHandle(MakeRequest());
    ok &= Expect(!exhausted_generation && exhausted_generation.GetError().HasCode("navigation.query_generation_exhausted"),
                 "query generation must become exhausted after maximum value");

    auto backend = std::make_shared<CountingNavigationBackend>();
    NavigationRuntime result_revision{NavigationOptions{}, NavigationDependencies{backend, {}, {}, {}}};
    const auto query = result_revision.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue() && AdvanceToPartiallyComplete(result_revision, query.Value()),
                "result revision test query should reach partial state"))
    {
        return false;
    }
    NavigationRuntimeTestAccess::SetResultRevision(result_revision, query.Value(), std::numeric_limits<std::uint64_t>::max());
    ok &= Expect(result_revision.Tick(RuntimeBudget{}) == 0, "result revision exhaustion must reject completion before callback");
    ok &= Expect(backend->calls == 0, "backend must not be called when result revision cannot be committed");
    ok &= ExpectPathState(result_revision, query.Value(), PathQueryState::PartiallyComplete,
                          "result revision exhaustion must preserve query state");
    return ok;
}

bool TestTerminalCancelReleaseAndStaleGenerationSemantics()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const auto cancelled = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(cancelled.HasValue(), "cancel test query should be accepted"))
    {
        return false;
    }
    bool ok = Expect(runtime.CancelPath(cancelled.Value()).HasValue(), "first cancel should succeed");
    ok &= Expect(runtime.CancelPath(cancelled.Value()).HasValue(), "repeated cancel should be idempotent");

    const auto completed = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(completed.HasValue(), "completed cancel test query should be accepted"))
    {
        return false;
    }
    (void)runtime.Tick(RuntimeBudget{.max_items = 1});
    (void)runtime.Tick(RuntimeBudget{.max_items = 1});
    (void)runtime.Tick(RuntimeBudget{.max_items = 1});
    const auto terminal_cancel = runtime.CancelPath(completed.Value());
    ok &= Expect(!terminal_cancel && terminal_cancel.GetError().HasCode("navigation.query_terminal"),
                 "completed query must reject cancellation");

    auto wrong_generation = completed.Value();
    ++wrong_generation.generation;
    const auto stale_generation = runtime.GetPathState(wrong_generation);
    ok &= Expect(!stale_generation && stale_generation.GetError().HasCode("navigation.stale_handle"),
                 "wrong generation must not observe an existing query record");

    ok &= Expect(runtime.ReleasePathResult(completed.Value()).HasValue(), "completed result release should succeed");
    const auto released_state = runtime.GetPathState(completed.Value());
    ok &= Expect(!released_state && released_state.GetError().HasCode("navigation.stale_handle"),
                 "released handle must become stale");
    return ok;
}

bool TestProviderAndBackendExceptionBoundaries()
{
    bool ok = true;

    auto obstacle = std::make_shared<CountingObstacleSource>();
    obstacle->should_throw = true;
    NavigationRuntime obstacle_runtime{NavigationOptions{}, NavigationDependencies{{}, {}, {}, obstacle}};
    const auto obstacle_query = obstacle_runtime.RequestPathHandle(MakeRequest());
    if (!Expect(obstacle_query.HasValue() && AdvanceToPartiallyComplete(obstacle_runtime, obstacle_query.Value()),
                "obstacle exception query should reach partial state"))
    {
        return false;
    }
    ok &= Expect(obstacle_runtime.Tick(RuntimeBudget{}) == 1, "obstacle provider exception should terminally process query");
    ok &= ExpectPathState(obstacle_runtime, obstacle_query.Value(), PathQueryState::Failed,
                          "obstacle provider exception must become controlled query failure");

    auto backend = std::make_shared<CountingNavigationBackend>();
    backend->mode = CountingNavigationBackend::Mode::Throw;
    NavigationRuntime backend_runtime{NavigationOptions{}, NavigationDependencies{backend, {}, {}, {}}};
    const auto backend_query = backend_runtime.RequestPathHandle(MakeRequest());
    if (!Expect(backend_query.HasValue() && AdvanceToPartiallyComplete(backend_runtime, backend_query.Value()),
                "throwing backend query should reach partial state"))
    {
        return false;
    }
    ok &= Expect(backend_runtime.Tick(RuntimeBudget{}) == 1, "backend exception should terminally process query");
    ok &= ExpectPathState(backend_runtime, backend_query.Value(), PathQueryState::Failed,
                          "backend exception must become controlled query failure");

    auto source = std::make_shared<ThrowAfterFirstRevisionSource>();
    NavigationRuntime source_runtime{NavigationOptions{}, NavigationDependencies{{}, source, {}, {}}};
    const auto source_query = source_runtime.RequestPathHandle(MakeRequest());
    if (!Expect(source_query.HasValue(), "source should succeed during request capture"))
    {
        return false;
    }
    ok &= Expect(source_runtime.Tick(RuntimeBudget{}) == 1, "later source exception should terminally process query");
    ok &= ExpectPathState(source_runtime, source_query.Value(), PathQueryState::Failed,
                          "CurrentRevision exception during Tick must become controlled query failure");

    auto invalid_cost = std::make_shared<CountingCostProvider>();
    invalid_cost->cost = std::numeric_limits<float>::quiet_NaN();
    NavigationRuntime invalid_cost_runtime{NavigationOptions{}, NavigationDependencies{{}, {}, invalid_cost, {}}};
    const auto invalid_cost_query = invalid_cost_runtime.RequestPathHandle(MakeRequest());
    if (!Expect(invalid_cost_query.HasValue() && AdvanceToPartiallyComplete(invalid_cost_runtime, invalid_cost_query.Value()),
                "invalid cost query should reach partial state"))
    {
        return false;
    }
    ok &= Expect(invalid_cost_runtime.Tick(RuntimeBudget{}) == 1, "invalid provider cost should terminally process query");
    ok &= ExpectPathState(invalid_cost_runtime, invalid_cost_query.Value(), PathQueryState::Failed,
                          "non-finite provider cost must not publish a path");
    return ok;
}

bool TestBackendResultValidationAudit()
{
    const CountingNavigationBackend::Mode invalid_modes[] = {
        CountingNavigationBackend::Mode::WrongState,
        CountingNavigationBackend::Mode::WrongRevision,
        CountingNavigationBackend::Mode::WrongStart,
        CountingNavigationBackend::Mode::WrongTarget,
        CountingNavigationBackend::Mode::NonFinite,
        CountingNavigationBackend::Mode::TooShort,
    };

    bool ok = true;
    for (const auto mode : invalid_modes)
    {
        auto backend = std::make_shared<CountingNavigationBackend>();
        backend->mode = mode;
        NavigationRuntime runtime{NavigationOptions{}, NavigationDependencies{backend, {}, {}, {}}};
        const auto query = runtime.RequestPathHandle(MakeRequest());
        if (!Expect(query.HasValue() && AdvanceToPartiallyComplete(runtime, query.Value()),
                    "invalid backend result query should reach partial state"))
        {
            return false;
        }
        ok &= Expect(runtime.Tick(RuntimeBudget{}) == 1, "invalid backend result should terminally process query");
        ok &= ExpectPathState(runtime, query.Value(), PathQueryState::Failed,
                              "invalid backend result must be rejected before publication");
        ok &= Expect(backend->calls == 1, "invalid backend result should be evaluated exactly once");
    }
    return ok;
}

bool TestReferenceBudgetUsesActualPreparedShape()
{
    auto obstacles = std::make_shared<CountingObstacleSource>();
    obstacles->obstacles.push_back(DynamicObstacle{DynamicObstacleId{9}, RegionId{7}, {}, false, 1.0f});
    auto costs = std::make_shared<CountingCostProvider>();
    costs->cost = 2.0f;
    NavigationRuntime runtime{NavigationOptions{}, NavigationDependencies{{}, {}, costs, obstacles}};
    const auto query = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue() && AdvanceToPartiallyComplete(runtime, query.Value()),
                "reference budget query should reach partial state"))
    {
        return false;
    }

    bool ok = Expect(runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 2}) == 0,
                     "two-point budget must reject a prepared three-point reference path");
    ok &= ExpectPathState(runtime, query.Value(), PathQueryState::PartiallyComplete,
                          "insufficient byte budget must preserve partial query state");
    ok &= Expect(obstacles->calls == 1 && costs->calls == 1,
                 "reference shape providers should be evaluated exactly once when plan is prepared");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 2}) == 0,
                 "repeated insufficient budget must keep prepared path pending");
    ok &= Expect(obstacles->calls == 1 && costs->calls == 1,
                 "prepared reference plan must avoid repeated provider calls");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 3}) == 1,
                 "three-point budget should publish prepared reference path");
    const auto result = runtime.GetPathResult(query.Value());
    ok &= Expect(result.HasValue() && result.Value().points.size() == 3,
                 "published reference path shape must match the budgeted shape");
    return ok;
}

bool TestBackendResultOwnershipSurvivesBudgetAndAllocationFault()
{
    bool ok = true;
    auto budget_backend = std::make_shared<CountingNavigationBackend>();
    budget_backend->point_count = 3;
    NavigationRuntime budget_runtime{NavigationOptions{}, NavigationDependencies{budget_backend, {}, {}, {}}};
    const auto budget_query = budget_runtime.RequestPathHandle(MakeRequest());
    if (!Expect(budget_query.HasValue() && AdvanceToPartiallyComplete(budget_runtime, budget_query.Value()),
                "backend budget query should reach partial state"))
    {
        return false;
    }
    ok &= Expect(budget_runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 2}) == 0,
                 "actual backend path bytes must respect byte budget");
    ok &= Expect(budget_backend->calls == 1, "backend should run once before budget defers publication");
    ok &= Expect(budget_runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 2}) == 0,
                 "pending backend result must remain deferred under the same budget");
    ok &= Expect(budget_backend->calls == 1, "deferred backend result must not re-execute backend");
    ok &= Expect(budget_runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 3}) == 1,
                 "larger byte budget should publish pending backend result");
    ok &= Expect(budget_backend->calls == 1, "publishing pending backend result must not re-execute backend");

    auto fault_backend = std::make_shared<CountingNavigationBackend>();
    fault_backend->point_count = 3;
    NavigationRuntime fault_runtime{NavigationOptions{}, NavigationDependencies{fault_backend, {}, {}, {}}};
    const auto fault_query = fault_runtime.RequestPathHandle(MakeRequest());
    if (!Expect(fault_query.HasValue() && AdvanceToPartiallyComplete(fault_runtime, fault_query.Value()),
                "backend allocation-fault query should reach partial state"))
    {
        return false;
    }
    NavigationRuntimeTestAccess::FailAfterBackendSuccess(fault_runtime);
    bool threw = false;
    try
    {
        (void)fault_runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 3});
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    ok &= Expect(threw, "injected local allocation failure should propagate as bad_alloc");
    ok &= Expect(fault_backend->calls == 1, "successful backend result must already have durable ownership at local failure");
    ok &= ExpectPathState(fault_runtime, fault_query.Value(), PathQueryState::PartiallyComplete,
                          "local allocation failure must preserve semantic query state");
    ok &= Expect(fault_runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 3}) == 1,
                 "retry should publish the durably held backend result");
    ok &= Expect(fault_backend->calls == 1, "retry after local failure must not call backend again");
    return ok;
}

bool TestReferencePlanSurvivesLocalAllocationFailure()
{
    auto obstacles = std::make_shared<CountingObstacleSource>();
    obstacles->obstacles.push_back(DynamicObstacle{DynamicObstacleId{5}, RegionId{7}, {}, false, 1.0f});
    auto costs = std::make_shared<CountingCostProvider>();
    NavigationRuntime runtime{NavigationOptions{}, NavigationDependencies{{}, {}, costs, obstacles}};
    const auto query = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(query.HasValue() && AdvanceToPartiallyComplete(runtime, query.Value()),
                "reference allocation-fault query should reach partial state"))
    {
        return false;
    }

    NavigationRuntimeTestAccess::FailNextReferenceResultAllocation(runtime);
    bool threw = false;
    try
    {
        (void)runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 3});
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    bool ok = Expect(threw, "reference result allocation failure should propagate as bad_alloc");
    ok &= Expect(obstacles->calls == 1 && costs->calls == 1,
                 "reference providers should have one durably recorded plan before local failure");
    ok &= ExpectPathState(runtime, query.Value(), PathQueryState::PartiallyComplete,
                          "reference allocation failure must preserve query state");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_bytes = sizeof(Vec3) * 3}) == 1,
                 "retry should build from the retained reference plan");
    ok &= Expect(obstacles->calls == 1 && costs->calls == 1,
                 "retry after local allocation failure must not repeat provider calls");
    return ok;
}

bool TestPurgeOccursOnlyAfterAllFallibleWorkListPreparation()
{
    NavigationRuntime runtime{NavigationOptions{}};
    auto expiring_request = MakeRequest();
    expiring_request.result_ttl = std::chrono::microseconds{1};
    const auto expiring = runtime.RequestPathHandle(expiring_request);
    if (!Expect(expiring.HasValue(), "expiring query should be accepted"))
    {
        return false;
    }
    (void)runtime.Tick(RuntimeBudget{});
    (void)runtime.Tick(RuntimeBudget{});
    (void)runtime.Tick(RuntimeBudget{});
    std::this_thread::sleep_for(std::chrono::milliseconds{1});

    const auto pending = runtime.RequestPathHandle(MakeRequest());
    if (!Expect(pending.HasValue(), "pending query should exist for processing-list allocation"))
    {
        return false;
    }

    NavigationRuntimeTestAccess::FailNextQueryWorkListAllocation(runtime);
    bool threw = false;
    try
    {
        (void)runtime.Tick(RuntimeBudget{});
    }
    catch (const std::bad_alloc&)
    {
        threw = true;
    }
    bool ok = Expect(threw, "query work-list allocation fault should propagate");
    ok &= ExpectPathState(runtime, expiring.Value(), PathQueryState::Stale,
                          "expired record must remain owned when later work-list preparation fails");
    ok &= ExpectPathState(runtime, pending.Value(), PathQueryState::Pending,
                          "processing-list allocation failure must preserve pending query pre-state");

    (void)runtime.Tick(RuntimeBudget{.max_items = 1});
    const auto removed = runtime.GetPathState(expiring.Value());
    ok &= Expect(!removed && removed.GetError().HasCode("navigation.stale_handle"),
                 "expired record should purge after all work lists prepare successfully");
    return ok;
}
} // namespace

int main()
{
    bool ok = true;
    ok &= TestRequestPathCompletesThroughBudgetedStates();
    ok &= TestPathBudgetOrderIsDeterministic();
    ok &= TestCancelledQueryDoesNotProducePath();
    ok &= TestCancelPartiallyCompletedQuery();
    ok &= TestTileDirtyRebuildStates();
    ok &= TestCostAndObstacleProjectionsStayExternal();
    ok &= TestInvalidInputsReturnFailures();
    ok &= TestFactoryBackendSelectionAndFailure();
    ok &= TestHandleReleaseTtlAndStaleRevision();
    ok &= TestSourceRevisionZeroBecomesStaleAfterAdvance();
    ok &= TestInvalidNumericPathRequestsAreRejected();
    ok &= TestProviderExceptionsBecomeFailures();
    ok &= TestRepeatedDirtyIsNoOp();
    ok &= TestByteBudgetLimitsCompletion();
    ok &= TestTileRegistrationTransitionBudgetAndRevisionAudit();
    ok &= TestQueryIdentityGenerationAndResultRevisionExhaustion();
    ok &= TestTerminalCancelReleaseAndStaleGenerationSemantics();
    ok &= TestProviderAndBackendExceptionBoundaries();
    ok &= TestBackendResultValidationAudit();
    ok &= TestReferenceBudgetUsesActualPreparedShape();
    ok &= TestBackendResultOwnershipSurvivesBudgetAndAllocationFault();
    ok &= TestReferencePlanSurvivesLocalAllocationFailure();
    ok &= TestPurgeOccursOnlyAfterAllFallibleWorkListPreparation();
    return ok ? 0 : 1;
}



