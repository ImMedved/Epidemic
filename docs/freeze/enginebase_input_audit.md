# EngineBase/Input local freeze audit

Scope: Goal 2.6. Module: `EngineBase/Input`, target `EpidemicPlatformInputIntegrationTests`.

Status: `LOCAL_READY` after strict MSVC Debug/Release execution on Windows 11.

## Reviewed state and ownership

Input is the sole owner of queued raw platform events, persistent keyboard/button state, mouse position/focus/capture, the published immutable frame snapshot, ordered transient events and the publication cursor. Platform owns native events; gameplay remains a consumer and cannot mutate Input state through the snapshot.

## Public contracts

Queue calls stage events without changing the current snapshot. `PublishSnapshot()` consumes the staged sequence once, preserves held state, clears transient state at the next publication and publishes the final state plus ordered transitions. `Reset()` clears pending, persistent and published state without manufacturing gameplay events. Invalid key/button values are ignored and never alias storage slot zero.

## State, boundaries and no-op semantics

Press, hold, release, repeated release, focus loss, empty publication and reset have explicit behavior. Per-event mouse deltas remain distinct while the snapshot accumulates frame motion. Wheel and mouse-delta arithmetic clamps through 64-bit intermediates, and the `uint64_t` publication cursor saturates rather than wrapping to a prior snapshot identity.

## Failure atomicity

Malformed raw key/button values and duplicate releases do not corrupt held or transient state and do not publish false normalized transitions. Input has no external transactional prepare/commit protocol, fallible rollback, or multi-container durable commit.

## Lifecycle and persistence

Input has no separate boot/shutdown state machine and owns no external resource registration. `Reset()` is the explicit transient-state restart. Input exposes no persistent snapshot/restore format, so persistence and cleanup-retry criteria are not applicable.

## Defect regressions

- Invalid enums no longer alias a valid backing-array slot.
- Release of an already released key/button no longer raises a false snapshot flag.
- Each mouse-move event keeps its own delta while the frame snapshot accumulates motion.
- Publication index, wheel accumulation and mouse deltas no longer wrap or execute signed-overflow arithmetic.

## Windows verification

MSVC `19.50.35729`, C++20, `/W4 /WX`: Debug and Release build passed. The merged Platform/Input executable passes raw Win32-to-Input integration plus keyboard, mouse, invalid-input, reset, empty-publish, multi-transition and numeric-boundary cases in the complete Base `14/14` CTest profiles.

## Corrective patch 2026-09-18

`INPUT-005`: `PublishSnapshot()` previously mutated keyboard/mouse state while iterating pending raw events and only later appended transient events. A `std::bad_alloc` could therefore leave a partially advanced unpublished frame and lose retry equivalence. Publication now builds keyboard state, mouse state, transient events, snapshot and next cursor entirely as candidates. Live state is changed only after all fallible work completes, and the raw pending queue is cleared only after commit.

`TestInputPublishAllocationFailureAtomicity` counts the actual publication allocation boundary, injects failure there, compares the previous published state and then retries the same pending raw sequence. A standalone strict GCC execution of the same regression also passed in the patch environment. The existing process-wide allocator sweep remains excluded from MSVC Debug STL for the previously documented debug-heap reason, so Windows qualification relies on the normal functional suite plus the repository's supported fault configurations.
