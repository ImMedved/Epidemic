# Runtime Update Order

Baseline frame order is owned by Support/composition code, not by direct calls between majors.

1. Advance `Time`.
2. Apply completed main-thread commits.
3. Process `Resources`.
4. Process `Streaming` budgets.
5. Tick `Simulation` jobs and memory expiration budgets.
6. Tick `Navigation` path and tile budgets.
7. Tick `Animation` playback and pose snapshots.
8. Step `Physics` fixed ticks.
9. Commit Scene-facing projections.
10. Update `Audio` emitters/listeners/events.
11. Prepare `Renderer`.
12. Publish diagnostics and event buffers.

Majors exchange data through public contracts, immutable snapshots, event buffers and Support adapters. This order is not permission for one major to include or call another major's private implementation.

## Allowed Adapters

- Scene -> Renderer
- Resources -> Renderer
- Scene -> Physics
- Resources -> Animation
- Animation -> Renderer
- Resources -> Audio
- Scene -> Audio
- World/Resources/Persistence -> Streaming
- Time -> Simulation
- Environment -> Audio
- Environment -> Navigation

Adapters live in Support/composition and contain no gameplay rules.

## Shutdown Order

1. Stop new work.
2. Cancel/wait background jobs.
3. Flush/discard proposals.
4. Stop audio.
5. Stop physics.
6. Release renderer.
7. Unload streaming.
8. Close persistence transactions.
9. Destroy services.
