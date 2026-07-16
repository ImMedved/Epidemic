# Runtime Update Order

Baseline frame order is owned by Support/composition code, not by direct calls between majors.

1. Advance `Time`.
2. Apply completed main-thread commits.
3. Process `Streaming` budgets.
4. Tick `Simulation` jobs and memory expiration budgets.
5. Tick `Navigation` path and tile budgets.
6. Tick `Animation` playback and pose snapshots.
7. Step `Physics` fixed ticks.
8. Commit Scene-facing projections.
9. Update `Audio` emitters/listeners/events.
10. Prepare `Renderer`.
11. Publish diagnostics and event buffers.

Majors exchange data through public contracts, immutable snapshots, event buffers and Support adapters. This order is not permission for one major to include or call another major's private implementation.
