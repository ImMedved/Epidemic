# Navigation

## Purpose

`Navigation` owns navigation tile lifecycle, budgeted path query lifecycle, traversal cost queries and dynamic obstacle projections. It does not make AI or movement decisions.

## Public Contracts

- `navigation_types.h`: tile ids, query ids, generation handles, tile/query states, path requests/results, nav revision, cost queries and obstacle snapshots.
- `navigation_runtime.h`: tile registry, path runtime, backend/data/obstacle source ownership contracts, cost provider, obstacle projection compatibility contract, `CreateNavigationServices()` and `CreateMockNavigationServices()`.

## Rules

- Path requests require a valid region id.
- Tile registration requires a valid tile id.
- `Tick(RuntimeBudget)` advances pending/running path queries in deterministic query-id order.
- `RebuildDirtyTiles(RuntimeBudget)` advances dirty/rebuilding tiles in deterministic tile-id order.
- Query states are explicit: `Pending`, `Running`, `PartiallyComplete`, `Completed`, `Failed`, `Cancelled` and `Stale`.
- `PathResult::revision` starts at `1` and increments on query state transitions.
- Cancelled queries remain observable as cancelled but do not expose path results.
- Released, TTL-expired, generation-stale or source-revision-stale path results fail with stale-result/handle errors.
- `PathResult::nav_revision` captures the source navigation revision. If the source revision changes, the query/result is reported as `Stale`; callers decide whether to recalculate.
- `RuntimeBudget::max_items` limits query transitions, `max_time` stops work after elapsed budget and `max_bytes` limits generated reference path point bytes.
- Production `CreateNavigationServices()` uses an injected backend when present, regardless of `enable_mock_queries`.
- Backend failure is not replaced by a reference mock result.
- Backend absence with `enable_mock_queries == false` fails with `navigation.backend_missing`.
- Mock path generation is available only through the mock-enabled reference profile or `CreateMockNavigationServices()`.
- Cost and obstacle data arrive through projection contracts; Navigation does not depend on Environment, Physics, Simulation or gameplay implementations.

## Testing Strategy

The `Navigation` tests cover backend selection, backend failure without mock fallback, reference profile selection, budgeted query states, deterministic query ordering, partial cancellation, tile rebuild lifecycle, external backend/cost/obstacle projections, source revision staleness, result TTL/release, byte budget limits, service factory shape and invalid input errors.
