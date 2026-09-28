# Cross-engine error and exception policy

This policy is part of the Goal 1 freeze contract. Module audits may narrow it, but any deviation must be recorded in that module's reviewed dossier and LOCAL_READY evidence.

## Failure categories

- Expected operational failures use `foundation::Result<T>` or an explicitly documented status/value. They do not throw and do not publish a false revision, event, journal entry, completion, or partial owner state.
- Programmer contract violations use assertions in debug builds only when continuing would be invalid. Public fallible input still receives a controlled failure in every build.
- Allocation failure may propagate `std::bad_alloc` unless an API explicitly documents a `Result` conversion. Fault injection must preserve the pre-call observable state.
- User callbacks, ports, sinks, providers, loaders, executors and backends are treated as throwing/fallible boundaries. State is committed only after required external preparation succeeds, or a durable reconciliation record is retained when rollback can fail.
- Destructors and scope guards do not emit exceptions. Fallible cleanup is exposed as an explicit operation; destructor cleanup is best-effort and must not hide an earlier failure.

## Mutation semantics

- A rejected or failed mutation leaves authoritative state and derived indexes mutually consistent.
- A semantic no-op has an explicit contract. Unless that contract says otherwise, it does not increment revisions/cursors, append journals, publish events, or invoke external effects.
- Partial initialization releases completed stages in reverse dependency order. Shutdown is idempotent where the public lifecycle permits repeated calls.
- Catch-all exception handling is allowed only at a documented containment boundary and must preserve the original diagnostic context.

## Compatibility boundary

The freeze promises reviewed C++ source/API compatibility for public declarations and behavior. It does not claim a stable cross-compiler C++ binary ABI. ABI stability requires a separate C-compatible or versioned binary boundary.
