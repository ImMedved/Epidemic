# LOCAL_READY contract

This is the single module-level admission contract used by Goals 2-4. It defines evidence requirements; it does not mark unaudited modules ready.

Contract inventory: `37` criteria for exactly `78` production modules.

A module may be `LOCAL_READY` only when every criterion is `PASS` or an explicitly permitted `N/A`. A `PASS` needs anchored evidence of every listed kind. An `N/A` needs a module-specific rationale and is rejected where the criterion is unconditional.

Every evidence anchor uses `path[:line]::symbol-or-case`; the validator resolves the repository-relative file, optional line and symbol. Callable evidence uses `public-header:line::fully-qualified-signature`; overloads therefore cannot collapse to one name. Test evidence names a CTest-registered executable and exact case/function, and supplies separately validated assertion anchors rather than a textual API-name match. State/fault evidence records the compared pre-state, injected failure point and observable post-state.

## Architecture and ownership

### ARCH-RESPONSIBILITY

- Requirement: One defined responsibility.
- Applicability: all modules.
- Required evidence kinds: `architecture`.
- `N/A`: not permitted.

### ARCH-SINGLE-OWNER

- Requirement: No second authoritative owner of the same data.
- Applicability: all modules.
- Required evidence kinds: `architecture`, `state`.
- `N/A`: not permitted.

### ARCH-DIRECT-DEPS

- Requirement: No hidden peer/lower-level dependency.
- Applicability: all modules.
- Required evidence kinds: `architecture`.
- `N/A`: not permitted.

### ARCH-PORTS

- Requirement: External systems are used through approved interfaces/ports.
- Applicability: all modules.
- Required evidence kinds: `architecture`, `contract`.
- `N/A`: not permitted.

## Public contracts

### API-CLASSIFIED

- Requirement: Every public callable is classified.
- Applicability: all modules; one row per exact qualified signature.
- Required evidence kinds: `contract`.
- `N/A`: not permitted.

### API-PRECONDITIONS

- Requirement: Every mutator has explicit preconditions.
- Applicability: all mutators.
- Required evidence kinds: `contract`, `test`.
- `N/A`: not permitted.

### API-SUCCESS

- Requirement: Every mutator has success postconditions.
- Applicability: all mutators.
- Required evidence kinds: `contract`, `test`.
- `N/A`: not permitted.

### API-FAILURE

- Requirement: Every mutator has failure and exception semantics.
- Applicability: all mutators.
- Required evidence kinds: `contract`, `fault`, `test`.
- `N/A`: not permitted.

### API-OVERLOADS

- Requirement: Overloads are separate contracts.
- Applicability: all public callable inventories.
- Required evidence kinds: `contract`.
- `N/A`: not permitted.

### API-INVALID

- Requirement: Invalid enums/IDs, malformed payloads and stale handles are rejected unambiguously.
- Applicability: modules exposing any such input.
- Required evidence kinds: `contract`, `test`.
- `N/A`: permitted with rationale.

## State consistency

### STATE-PRIMARY

- Requirement: Primary state remains internally consistent.
- Applicability: stateful modules.
- Required evidence kinds: `state`, `test`.
- `N/A`: permitted with rationale.

### STATE-INDEXES

- Requirement: Indexes agree with primary state.
- Applicability: modules with indexes/caches.
- Required evidence kinds: `state`, `test`.
- `N/A`: permitted with rationale.

### STATE-COUNTERS

- Requirement: Generators, generations, revisions and cursors have checked boundary behavior.
- Applicability: modules exposing those counters.
- Required evidence kinds: `state`, `test`.
- `N/A`: permitted with rationale.

### STATE-NO-FALSE-PUBLISH

- Requirement: Failed operations publish no false revision/event/journal mutation.
- Applicability: modules publishing observable change.
- Required evidence kinds: `fault`, `state`, `test`.
- `N/A`: permitted with rationale.

### STATE-NOOP

- Requirement: No-op semantics are explicit.
- Applicability: all mutators.
- Required evidence kinds: `contract`, `test`.
- `N/A`: not permitted.

## Lifecycle

### LIFE-ALLOWED

- Requirement: Every allowed transition is checked.
- Applicability: modules with lifecycle state.
- Required evidence kinds: `lifecycle`, `test`.
- `N/A`: permitted with rationale.

### LIFE-FORBIDDEN

- Requirement: Every forbidden transition is rejected.
- Applicability: modules with lifecycle state.
- Required evidence kinds: `lifecycle`, `test`.
- `N/A`: permitted with rationale.

### LIFE-SHUTDOWN

- Requirement: Shutdown and cleanup semantics are checked.
- Applicability: modules owning resources or registrations.
- Required evidence kinds: `lifecycle`, `test`.
- `N/A`: permitted with rationale.

### LIFE-RETRY-CLEANUP

- Requirement: Cleanup retry is checked when external cleanup can fail.
- Applicability: fallible external cleanup only.
- Required evidence kinds: `fault`, `lifecycle`, `test`.
- `N/A`: permitted with rationale.

## Failure atomicity

### ATOMIC-SINGLE

- Requirement: Single-record mutation leaves no partial record.
- Applicability: stateful mutators.
- Required evidence kinds: `fault`, `state`, `test`.
- `N/A`: permitted with rationale.

### ATOMIC-MULTI

- Requirement: Multi-container mutation commits all or nothing.
- Applicability: mutators spanning multiple containers.
- Required evidence kinds: `fault`, `state`, `test`.
- `N/A`: permitted with rationale.

### ATOMIC-EXTERNAL

- Requirement: External prepare/commit/cancel/rollback contracts are checked.
- Applicability: modules with external transactional work.
- Required evidence kinds: `contract`, `fault`, `test`.
- `N/A`: permitted with rationale.

### ATOMIC-RECONCILE

- Requirement: Durable reconciliation is used when rollback can fail.
- Applicability: modules with fallible rollback.
- Required evidence kinds: `contract`, `fault`, `state`, `test`.
- `N/A`: permitted with rationale.

## Persistence

### PERSIST-SNAPSHOT

- Requirement: Snapshot is complete.
- Applicability: persistent modules.
- Required evidence kinds: `persistence`, `state`, `test`.
- `N/A`: permitted with rationale.

### PERSIST-VALIDATE

- Requirement: Restore validates fully before live mutation.
- Applicability: persistent modules.
- Required evidence kinds: `fault`, `persistence`, `test`.
- `N/A`: permitted with rationale.

### PERSIST-CANDIDATE

- Requirement: Restore builds a candidate off-state.
- Applicability: persistent modules.
- Required evidence kinds: `persistence`, `state`, `test`.
- `N/A`: permitted with rationale.

### PERSIST-FAILURE

- Requirement: Failed restore preserves live state.
- Applicability: persistent modules.
- Required evidence kinds: `fault`, `persistence`, `state`, `test`.
- `N/A`: permitted with rationale.

### PERSIST-CONTINUITY

- Requirement: Successful restore preserves generators, generations, revisions, cursors and the persistent/transient boundary.
- Applicability: persistent modules.
- Required evidence kinds: `persistence`, `state`, `test`.
- `N/A`: permitted with rationale.

## Tests

### TEST-HAPPY

- Requirement: Happy path.
- Applicability: all modules.
- Required evidence kinds: `test`.
- `N/A`: not permitted.

### TEST-INVALID

- Requirement: Invalid input.
- Applicability: modules accepting fallible input.
- Required evidence kinds: `test`.
- `N/A`: permitted with rationale.

### TEST-DUPLICATE

- Requirement: Duplicate identity.
- Applicability: modules with identity registration/creation.
- Required evidence kinds: `test`.
- `N/A`: permitted with rationale.

### TEST-STALE

- Requirement: Stale identity.
- Applicability: modules with removable/reusable identities.
- Required evidence kinds: `test`.
- `N/A`: permitted with rationale.

### TEST-EMPTY

- Requirement: Empty state.
- Applicability: stateful or collection modules.
- Required evidence kinds: `test`.
- `N/A`: permitted with rationale.

### TEST-BOUNDARY

- Requirement: Boundary, overflow and underflow.
- Applicability: modules with numeric limits/counters.
- Required evidence kinds: `test`.
- `N/A`: permitted with rationale.

### TEST-WRONG-LIFECYCLE

- Requirement: Wrong lifecycle state.
- Applicability: modules with lifecycle state.
- Required evidence kinds: `test`.
- `N/A`: permitted with rationale.

### TEST-CALLBACK-FAILURE

- Requirement: Callback/backend exception or failure.
- Applicability: modules invoking callbacks/backends.
- Required evidence kinds: `fault`, `test`.
- `N/A`: permitted with rationale.

### TEST-REGRESSION

- Requirement: Regression test for every fixed defect.
- Applicability: all defects fixed during the current audit; PASS asserts the defect ledger was reviewed.
- Required evidence kinds: `test`.
- `N/A`: not permitted.

## Ledger workflow

`local_ready_ledger.json` is the authoritative audit ledger. The four verified architecture criteria are seeded as `PASS`; every other new criterion starts as `NOT_AUDITED`. `IN_AUDIT` and `BLOCKED` are intermediate module states. The validator rejects stale module/criterion sets, nonexistent anchor files/lines/symbols, unregistered test targets, missing assertion anchors, evidence-kind omissions, unjustified `N/A`, empty or inconsistent `BLOCKED`, inconsistent module states, and premature `LOCAL_READY`.

Run `python docs/freeze/local_ready_contract.py --check` for contract and ledger consistency and `python docs/freeze/local_ready_contract.py --self-test` for negative validator tests.
