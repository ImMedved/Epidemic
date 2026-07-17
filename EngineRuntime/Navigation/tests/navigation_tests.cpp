#include "navigation_runtime_impl.h"

#include <iostream>
#include <span>
#include <string_view>
#include <vector>

using epidemic::runtime::RegionId;
using epidemic::runtime::RuntimeBudget;
using epidemic::runtime::Vec3;
using epidemic::runtime::navigation::DynamicObstacle;
using epidemic::runtime::navigation::DynamicObstacleId;
using epidemic::runtime::navigation::IDynamicObstacleProjection;
using epidemic::runtime::navigation::INavigationBackend;
using epidemic::runtime::navigation::INavigationDataSource;
using epidemic::runtime::navigation::INavCostProvider;
using epidemic::runtime::navigation::CreateMockNavigationServices;
using epidemic::runtime::navigation::NavCostQuery;
using epidemic::runtime::navigation::NavTileId;
using epidemic::runtime::navigation::NavTileState;
using epidemic::runtime::navigation::NavigationRevision;
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

struct FixedObstacleProjection final : IDynamicObstacleProjection
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
    epidemic::foundation::Result<PathResult> BuildPath(const PathRequest& request, NavigationRevision revision) const override
    {
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
    return PathRequest{Vec3{0.0f, 0.0f, 0.0f}, Vec3{10.0f, 0.0f, 0.0f}, RegionId{7}, RuntimeBudget{}};
}

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
}

bool TestRequestPathCompletesThroughBudgetedStates()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const auto query = runtime.RequestPath(MakeRequest());
    if (!Expect(query.HasValue(), "path request should succeed"))
    {
        return false;
    }

    const auto id = query.Value();
    bool ok = Expect(runtime.GetPathState(id) == PathQueryState::Pending, "new query should start pending");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "first tick should advance one query");
    ok &= Expect(runtime.GetPathState(id) == PathQueryState::Running, "first tick should move query to running");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "second tick should complete query");
    ok &= Expect(runtime.GetPathState(id) == PathQueryState::Completed, "second tick should complete query");
    const auto result = runtime.GetPathResult(id);
    ok &= Expect(result.HasValue(), "completed query should have result");
    ok &= Expect(result.HasValue() && result.Value().points.size() == 2, "default mock path should contain start and target");
    ok &= Expect(result.HasValue() && result.Value().revision == 3, "completed query should have three revisions");
    return ok;
}

bool TestPathBudgetOrderIsDeterministic()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const auto first = runtime.RequestPath(MakeRequest());
    const auto second = runtime.RequestPath(MakeRequest());
    if (!Expect(first.HasValue() && second.HasValue(), "path requests should succeed"))
    {
        return false;
    }

    bool ok = Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "budget should advance one query");
    ok &= Expect(runtime.GetPathState(first.Value()) == PathQueryState::Running, "first query should advance first");
    ok &= Expect(runtime.GetPathState(second.Value()) == PathQueryState::Pending, "second query should wait");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "budget should advance next query");
    ok &= Expect(runtime.GetPathState(first.Value()) == PathQueryState::Completed, "first query should complete before second starts");
    ok &= Expect(runtime.GetPathState(second.Value()) == PathQueryState::Pending, "second query should still wait");
    return ok;
}

bool TestCancelledQueryDoesNotProducePath()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const auto query = runtime.RequestPath(MakeRequest());
    if (!Expect(query.HasValue(), "path request should succeed before cancellation"))
    {
        return false;
    }

    const auto id = query.Value();
    bool ok = Expect(runtime.CancelPath(id).HasValue(), "pending query should cancel");
    ok &= Expect(runtime.GetPathState(id) == PathQueryState::Cancelled, "cancelled query should report cancelled");
    ok &= Expect(!runtime.GetPathResult(id).HasValue(), "cancelled query should not expose path result");
    ok &= Expect(runtime.Tick(RuntimeBudget{}) == 0, "cancelled query should not consume tick work");
    return ok;
}

bool TestTileDirtyRebuildStates()
{
    NavigationRuntime runtime{NavigationOptions{}};
    const NavTileId tile{42};
    bool ok = Expect(runtime.GetTileState(tile) == NavTileState::Missing, "unknown tile should be missing");
    ok &= Expect(runtime.RegisterTile(tile, NavTileState::Ready).HasValue(), "valid tile should register");
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
    NavigationRuntime runtime{NavigationOptions{}};
    FixedCostProvider costs{};
    FixedObstacleProjection obstacles{};
    obstacles.obstacles.push_back(DynamicObstacle{DynamicObstacleId{5}, RegionId{7}, {}, true, 1.0f});
    runtime.SetProjectionSources(&costs, &obstacles);

    const auto query = runtime.RequestPath(MakeRequest());
    if (!Expect(query.HasValue(), "path request with projection sources should succeed"))
    {
        return false;
    }

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
    bool ok = Expect(!runtime.RequestPath(PathRequest{}).HasValue(), "invalid region should fail path request");
    ok &= Expect(!runtime.RegisterTile(NavTileId{}, NavTileState::Ready).HasValue(), "invalid tile id should fail registration");
    ok &= Expect(!runtime.MarkTileDirty(NavTileId{99}).HasValue(), "unknown tile should fail dirty mark");
    return ok;
}

bool TestHandleReleaseStaleAndFactoryContracts()
{
    NavigationRuntime runtime{NavigationOptions{}};
    FixedNavigationBackend backend{};
    FixedNavigationDataSource data_source{};
    runtime.SetBackendSources(&backend, &data_source);

    auto request = MakeRequest();
    request.source_revision = 1;
    const auto handle = runtime.RequestPathHandle(request);
    if (!Expect(handle.HasValue(), "path handle request should succeed"))
    {
        return false;
    }

    bool ok = Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "handle query should enter running");
    ok &= Expect(runtime.Tick(RuntimeBudget{.max_items = 1}) == 1, "handle query should complete");
    const auto result = runtime.GetPathResult(handle.Value());
    ok &= Expect(result.HasValue(), "handle query should expose completed result");
    ok &= Expect(result.HasValue() && result.Value().stale, "source revision mismatch should mark result stale");
    ok &= Expect(result.HasValue() && result.Value().nav_revision == 99, "data source revision should be captured");
    ok &= Expect(runtime.ReleasePathResult(handle.Value()).HasValue(), "result release should succeed");
    ok &= Expect(!runtime.GetPathResult(handle.Value()).HasValue(), "released result should be unavailable");

    const auto services = CreateMockNavigationServices();
    ok &= Expect(services.runtime != nullptr && services.tiles != nullptr, "mock navigation services should be populated");
    return ok;
}
} // namespace

int main()
{
    bool ok = true;
    ok &= TestRequestPathCompletesThroughBudgetedStates();
    ok &= TestPathBudgetOrderIsDeterministic();
    ok &= TestCancelledQueryDoesNotProducePath();
    ok &= TestTileDirtyRebuildStates();
    ok &= TestCostAndObstacleProjectionsStayExternal();
    ok &= TestInvalidInputsReturnFailures();
    ok &= TestHandleReleaseStaleAndFactoryContracts();
    return ok ? 0 : 1;
}


