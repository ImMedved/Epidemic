# EngineBase/Diagnostics local freeze audit

Audit scope: Goal 2.3 only. The reviewed production unit is `EngineBase/Diagnostics` / `EpidemicDiagnostics`. The module remains observational. It owns diagnostic state only and is not an authoritative owner for application, runtime, or gameplay data.

## Reviewed state model

Diagnostic-only state consists of the fixed atomic counter array, the process logging/profiling enable flags, the process profile collector reference, samples retained by `InMemoryProfileCollector`, `ConsoleLogger` output state, and one `thread_local` diagnostic name per thread. None of these values participates in engine decisions. The module has no persistent save/restore contract. `InMemoryProfileCollector::Snapshot()` is a detached diagnostic read copy, not a persistence snapshot.

There are no secondary indexes, generations, revisions, cursors, durable transactions, or cleanup/reconciliation state. `CounterId::Count` is a sentinel and is never a valid storage index.

## Public contract: logging

`SetLoggingEnabled` atomically changes only the observation gate. Repeating the same value is an observable no-op. `IsLoggingEnabled` returns that gate. Disabled logging returns before thread-name capture or diagnostic allocation, does not construct or dispatch a sink message, and does not replay skipped messages when re-enabled.

All public `ILogger::Log`, `Trace`, `Debug`, `Info`, `Warn`, `Error`, and `Fatal` entry points are `noexcept` containment boundaries. Borrowed module/category/message text must remain valid only until the call returns. `LogMessage::thread_name` is owned. When logging is enabled, exactly one `Write` attempt is made for one public log call. Any exception from the concrete sink is swallowed. When logging is disabled, `Write` is not invoked. Sink failure therefore cannot become control flow for authoritative state and cannot replace an exception already being handled.

`ConsoleLogger` implements only the private sink-side `Write` hook. File-system, formatting, console, and file failures are contained by `ILogger` at the public boundary. Diagnostics output is not an event bus and no caller may rely on successful output to commit engine state.

## Public contract: counters

`CounterCount` is the number of valid slots. `IsValidCounterId` accepts only values strictly below `CounterId::Count`. `ToString` returns stable names for valid counters and `unknown` for the sentinel or malformed enum values.

`DiagnosticsCounters::Increment` and `Decrement` accept signed deltas and perform atomic saturating arithmetic. Results clamp to `INT64_MIN` or `INT64_MAX` instead of wrapping. Invalid IDs are rejected as no-ops. `Set` stores the exact signed value for a valid ID and rejects invalid IDs as no-ops. `Get` returns the stored value for a valid ID and the neutral value `0` for an invalid ID. `Reset` stores zero into every valid slot. Zero deltas and setting an already-held value are observable no-ops.

The global registry returned by `GlobalCounters` is process-lifetime diagnostic state only. Counters are not revisions, ownership tokens, or synchronization primitives.

## Public contract: profiling

`SetProfileCollector` replaces the collector used by subsequently constructed scopes and accepts null to disable collection through absence of a sink. `GetProfileCollector` returns a shared owning reference. `SetProfilingEnabled` atomically changes the observation gate. Repeating the same enable state is an observable no-op.

`InMemoryProfileCollector::Record` appends one complete event or propagates its direct storage failure to a direct caller. `Snapshot` returns a detached copy. `Reset` removes retained diagnostic samples and is a no-op when already empty.

`ProfileScope` owns its scope name and captures one shared collector only if profiling is enabled when construction begins. The disabled path returns before copying the scope name, so it does not perform diagnostic allocation. A scope that started with no collector or with profiling disabled emits nothing. Replacing or clearing the global collector while a scope is active does not redirect that already-started event. The destructor is `noexcept`, emits at most one event on normal return and exception unwinding, and contains all collector exceptions so a profiling sink cannot replace the original failure.

The deleted copy constructor and copy assignment are compile-time lifecycle constraints. A `ProfileScope` has one stack lifetime and cannot be duplicated into a second completion event.

## Public contract: thread context

`SetCurrentThreadName` replaces only the calling thread's diagnostic name and accepts an empty name to clear it. `GetCurrentThreadName` returns an owning `std::string` copy. A previously returned value remains valid after later renames and after the originating thread exits. Thread-local names are diagnostic labels only and are never identity or ownership keys.

## Failure atomicity and no-op review

Logger and `ProfileScope` sink failures cannot mutate authoritative engine state because the public logging boundary and RAII destructor contain exceptions before they return to callers. Counter operations affect exactly one valid atomic slot or no slot. Invalid counter IDs cannot index the array. Profile collector replacement changes only one protected shared pointer. Thread-name replacement changes only one `thread_local` string. `InMemoryProfileCollector::Record` uses the strong exception guarantee of `std::vector::push_back`; a targeted `std::bad_alloc` regression verifies that failed append preserves the exact pre-state and the collector remains usable.

No multi-container engine transaction, external prepare/commit protocol, durable rollback, lifecycle shutdown, persistence restore, identity registration, or stale handle surface exists in this module. Those LOCAL_READY criteria are `N/A` for Diagnostics with this reviewed state model.

## Regression defects fixed by Goal 2.3

`DIAG-001 sink-exception-control-flow`: the old virtual public logger call allowed a throwing sink to escape through convenience logging and potentially mask an engine failure. The public `ILogger` entry points now contain sink exceptions.

`DIAG-002 counter-invalid-index`: `CounterId::Count` and arbitrary underlying enum values previously indexed the atomic array without validation. Invalid IDs are now rejected without storage access.

`DIAG-003 counter-wraparound`: counter increment/decrement previously used raw signed atomic arithmetic at integer boundaries. Operations now use checked saturating CAS updates, including the `INT64_MIN` delta case.

`DIAG-004 thread-name-borrow`: `GetCurrentThreadName()` previously returned a `string_view` into mutable thread-local string storage. It now returns an owning string and `LogMessage` owns the captured thread name.

`DIAG-005 scope-collector-redirection`: `ProfileScope` previously re-read the global collector in its destructor, so replacing the collector while a scope was active redirected the event. A scope now captures and retains the collector selected at construction.

`DIAG-006 disabled-profile-allocation`: disabled profiling previously copied the scope name before checking the enable gate, so a diagnostic allocation failure could affect otherwise disabled engine execution. The disabled path now returns before name capture, and a targeted allocation-fault regression verifies that no allocation is attempted.

## Coverage cross-check note

A direct review of the five Diagnostics public headers found 43 explicitly declared public callable contracts. The shared Goal 1 lexical coverage inventory currently emits 46 Diagnostics rows because it also classifies three initialized public data fields containing function calls (`LogMessage::timestamp`, `LogMessage::thread_id`, and `ProfileEvent::thread_id`) as callable rows. Those three rows are conservative false positives, not missing Diagnostics contracts. All 43 actual source-declared callables are present and reviewed, and all 46 generated rows have anchors. The shared scanner behavior predates this module audit and is recorded here as freeze-tooling debt rather than changed inside Goal 2.3.

## Goal 2.3 checklist result

[x] Disabled logger/profiler does not change engine behavior or invoke its sink.

[x] Counter increment/decrement normal and integer-boundary behavior is defined and tested.

[x] `ProfileScope` closes through RAII on normal return and exception unwinding.

[x] Thread diagnostic names use owning copies and do not expose dangling references.

[x] Sink failure is contained before it can become a hidden channel of authoritative state change.

[x] Logging from a failure path preserves the original error.

[x] Diagnostics is reviewed as observation-only state and is not an event bus or authoritative storage.

Module-local result: `LOCAL_READY`, subject to the unchanged Goal 1 architecture gates and later Goal 6 threading/lifetime qualification for whole-engine freeze.
