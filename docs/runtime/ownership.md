# Runtime Ownership

## Service Ownership

Factories return owned service objects. Support registers shared service bundles in the application service container.

`EngineRuntimeServices` owns the default composition aggregate returned by `RegisterDefaultEngineRuntime`: registered major names, `RuntimeIntegrationServices` and the `IEngineRuntimeCoordinator`. The application service container owns the registered `XxxServices` bundles.

## Snapshots

Query APIs return values such as `TimeSnapshot`, `WorldObjectRecord`, `StreamingProgress`, `PhysicsStepResult`, `PathResult`, `PoseSnapshot`, `AudioEmitterSnapshot`, persistence snapshots, environment snapshots and scene snapshots. These are immutable snapshots from the caller's point of view and externally visible state snapshots carry revisions.

## Handles

Runtime handles that can become stale carry a generation or document a never-reuse policy. Callers must validate handles through the owning major instead of assuming that a raw index remains valid after release/recreate cycles.

Resource dependencies acquired by `Resources` are owned handles, not borrowed references. Root load rollback, explicit eviction and budget eviction release those owned dependency handles through the same `Release()` contract used by external callers.

## Borrowed Dependencies

Projection/source interfaces are borrowed for call duration or registered as non-owning pointers when explicitly documented. Owners must outlive the runtime that uses them.

Support-owned adapters are the long-lived owners for default cross-major projections. They hold shared service references or resource leases where needed, and they are destroyed after consumers during shutdown. Raw pointers in module options are non-owning adapter ports; the owner is either the caller or `RuntimeIntegrationServices`.

## Event Buffers

Event buffers are owned by their major. Consumers read spans during frame orchestration and call `Clear()` only after dispatch.

## Raw Pointers

Undocumented raw ownership is forbidden. Raw pointers in options/contracts are non-owning adapter references unless a contract says otherwise.
