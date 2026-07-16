# Navigation

## Purpose

`Navigation` owns navigation tile lifecycle, budgeted path query lifecycle, traversal cost queries and dynamic obstacle projections. It does not make AI or movement decisions.

## Public Contracts

- `navigation_types.h`: tile ids, query ids, tile/query states, path requests/results, cost queries and obstacle snapshots.
- `navigation_runtime.h`: tile registry, path runtime, cost provider and obstacle projection contracts.

## Rules

- Path requests require a valid region id.
- Tile registration requires a valid tile id.
- `Tick(RuntimeBudget)` advances pending/running path queries in deterministic query-id order.
- `RebuildDirtyTiles(RuntimeBudget)` advances dirty/rebuilding tiles in deterministic tile-id order.
- `PathResult::revision` starts at `1` and increments on query state transitions.
- Cancelled queries remain observable as cancelled but do not expose path results.
- Cost and obstacle data arrive through projection contracts; Navigation does not depend on Environment, Physics, Simulation or gameplay implementations.

## Testing Strategy

The `Navigation` tests cover budgeted query states, deterministic query ordering, cancellation, tile rebuild lifecycle, external cost/obstacle projections and invalid input errors.
