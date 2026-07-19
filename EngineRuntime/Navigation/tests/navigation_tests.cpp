#include "navigation_runtime_impl.h"

#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

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

struct FixedNavigationDataSource final : INavigationDataSource
{
    NavigationRevision revision{99};

    NavigationRevision CurrentRevision(RegionId) const override
    {
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
    ok &= TestByteBudgetLimitsCompletion();
    return ok ? 0 : 1;
}



