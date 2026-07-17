# Runtime Threading Policy

EngineRuntime majors use a simple baseline threading model until a module documents a stricter contract.

## Baseline

- Mutation must run on the runtime/main thread or inside an explicitly controlled commit phase.
- Background jobs may only operate on detached immutable input.
- Commit work must return through `IMainThreadDispatcher` or a major-owned synchronization point.
- Snapshots are value objects or immutable published data.
- Registries are not thread-safe unless their public documentation explicitly says otherwise.

## Public Contract Notes

Mutable public registries and runtime managers repeat the short contract in their headers:

```cpp
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
```

The policy intentionally does not add mutexes to every class. Synchronization belongs to orchestration, commit phases or a module-specific implementation when that module has a real concurrency requirement.

## Frame Commit Rules

Support owns the default update order and shutdown order. Background work may prepare proposals from immutable snapshots, but authoritative mutation returns through the runtime thread, an explicit commit phase or a module-owned synchronization point. Generation-validated handles and revisioned snapshots are the expected way to detect stale work at commit time.
