# Runtime Ownership

## Service Ownership

Factories return owned service objects. Support registers shared service bundles in the application service container.

## Snapshots

Query APIs return values such as `TimeSnapshot`, `WorldObjectRecord`, `StreamingProgress`, `PhysicsStepResult`, `PathResult`, `PoseSnapshot` and `AudioEmitterSnapshot`. These are immutable snapshots from the caller's point of view.

## Borrowed Dependencies

Projection/source interfaces are borrowed for call duration or registered as non-owning pointers when explicitly documented. Owners must outlive the runtime that uses them.

## Event Buffers

Event buffers are owned by their major. Consumers read spans during frame orchestration and call `Clear()` only after dispatch.

## Raw Pointers

Undocumented raw ownership is forbidden. Raw pointers in options/contracts are non-owning adapter references unless a contract says otherwise.
