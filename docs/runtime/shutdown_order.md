# Runtime Shutdown Order

`IEngineRuntimeCoordinator::Shutdown()` is idempotent. A second call returns success after the first shutdown completes.

Default shutdown order:

1. Stop accepting new work.
2. Cancel or wait simulation, navigation and streaming jobs.
3. Discard or commit valid pending proposals.
4. Stop audio.
5. Stop physics.
6. Release animation and renderer resources.
7. Release renderer leases.
8. Unload streaming state.
9. Finish or rollback persistence transactions.
10. Flush persistence according to durability policy.
11. Destroy Support-owned adapters.
12. Destroy services.

Shutdown must preserve previous authoritative state when a pending streaming or simulation proposal is invalid. Persistence durability failures are reported through `Result`-based contracts; a failed flush must not be represented as a successful durable shutdown.
