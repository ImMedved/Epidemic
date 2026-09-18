# EngineBase Platform local freeze audit

Scope: Goal 2.5 only. Module: `EngineBase/Platform`, target `EpidemicPlatform`.

Status: `LOCAL_READY`. The original Linux syntax/stub audit was followed by strict MSVC Debug and Release execution of the real Win32 message pump on Windows 11; `EpidemicPlatformInputIntegrationTests` passes in both Base `10/10` profiles.

## Reviewed responsibility and ownership

Platform is the authoritative EngineBase owner of native window lifetime, normalized platform-event queuing, process metadata, platform exit observation, the monotonic platform clock boundary and dynamic-library handles. It does not own Input state, rendering/swap-chain state, UI state or gameplay state.

The Win32 implementation owns:

* one `RuntimeToken` connecting surviving window wrappers to their runtime;
* `windows_by_id` as primary window ownership and `windows_by_handle` as the native lookup index;
* monotonic `next_window_id`, with zero reserved as invalid and explicit exhaustion rejection;
* `queued_events`, drained by move/swap semantics;
* sticky `exit_requested`, runtime `shutdown`, owner-thread identity and window-class registration state;
* each window's `HWND`, cached client dimensions, DPI, focus, minimized, close-request, mouse-capture and button-mask state;
* each dynamic-library wrapper's single `HMODULE` ownership.

All of this state is transient. Platform exposes no snapshot/restore contract.

## Public contract: neutral boundary

`NativeWindowHandle` is an opaque pointer value. Default construction is invalid, `Value()`/`As<T>()` preserve the wrapped pointer, and equality compares native pointer identity. Public Platform headers expose no `Windows.h`, `HWND`, `HMODULE`, `HINSTANCE`, `WPARAM`, `LPARAM` or `LRESULT` type.

`ProcessInfo` is captured at runtime construction and exposed read-only. `WindowsPlatformRuntime::Name()` is stable diagnostic metadata.

## Public contract: runtime and clock

`Now()` is the Foundation steady-clock boundary and is monotonic for successive observations. It is not a wall-clock API.

`PumpEvents()` consumes the currently pending native Win32 messages exactly once. `IsExitRequested()` is sticky once the final tracked window is destroyed or `WM_QUIT` is observed. New native windows are rejected after terminal exit request or shutdown. Window lifecycle, message pumping, dynamic-library loading and shutdown are owner-thread-only. New mutation work is rejected after successful shutdown, while read-only observations and final event drainage remain available. `Shutdown()` is idempotent after successful cleanup.

Owner-thread mutation operations reject wrong-thread calls before native mutation. Read-only queue/count access is mutex-protected; `exit_requested` is atomic. This does not widen the module into a generally multi-writer thread-safe service.

## Public contract: windows and event queue

`CreateWindow()` rejects zero client dimensions. A logical `WindowId` is not committed until native creation and both runtime indexes are committed. Failure before commit leaves no tracked window and does not advance the id generator.

A tracked window follows `Created -> CloseRequested -> Destroyed`. `WM_CLOSE` is a close request only. It sets wrapper state and publishes at most one `WindowCloseRequested` event. Native destruction invalidates the wrapper handle, removes both runtime indexes and requests process exit only when the final tracked window disappears. `Close()` is idempotent after destruction.

During `CreateWindowExW`, native messages may arrive before runtime tracking is committed. Such construction-time messages update local wrapper state where needed but cannot publish observable Platform events until `MarkTracked()` commits ownership.

The WndProc takes a temporary `shared_ptr` keep-alive while dispatching. On `WM_DESTROY`, `GWLP_USERDATA` is cleared before the runtime releases its owning `shared_ptr`, preventing later native teardown messages from dereferencing a destroyed wrapper.

Resize publication is edge-based. Repeated equal positive dimensions do not republish resize. Minimize is published once on its transition. A zero client area does not produce a drawable resize or premature restore. The first subsequent positive restored area publishes one restore and one usable resize.

Focus gain/loss is edge-based. Mouse capture state is changed before calling `SetCapture`/`ReleaseCapture`, so reentrant `WM_CAPTURECHANGED` cannot duplicate capture transition events. Native key, mouse, wheel and movement messages are normalized once per message.

`DrainEvents()` removes the current queue; a second drain without new input is empty. Pumping again without new native messages cannot replay prior events.

## Public contract: dynamic libraries

`LoadDynamicLibrary()` is owner-thread work, requires a non-empty absolute path and is rejected after terminal runtime shutdown. Win32 load failure returns `platform.load_library_failed` without a published wrapper. A successfully acquired `HMODULE` is immediately guarded by local RAII until ownership transfers to `WindowsDynamicLibrary`, so C++ allocation/construction failure after `LoadLibraryExW` cannot leak the module.

`FindSymbol()` rejects an empty symbol name with `platform.empty_symbol_name`; an absent export returns `platform.symbol_not_found`. Destroying the final wrapper releases the owned `HMODULE` exactly once.

## Failure atomicity and no-op review

Rejected zero-size creation, wrong-thread creation and terminal-runtime creation do not publish a window. Native/C++ creation failure does not advance `next_window_id`. Two-index registration checks both insertions, rolls back the first map insertion if the second insertion fails, then destroys the native window. `TestWindowCreationAllocationFailureAtomicity` sweeps every observed C++ allocation boundary and verifies empty indexes, empty event publication and unchanged next id after each injected `std::bad_alloc`. `TestWindowIdBoundaryPolicy` separately verifies invalid zero, the final allocatable id and the exhausted sentinel without uint64 wrap.

Duplicate `WM_CLOSE`, duplicate focus messages, duplicate minimize messages and repeated same-size resize messages are observable no-ops. Repeated `Close()` after native destruction and repeated successful `Shutdown()` are no-ops. Repeated `DrainEvents()` does not replay state.

Platform persistence criteria are `N/A`: all authoritative state is process/native-runtime state and no snapshot API is exposed. Durable reconciliation and external prepare/commit/rollback are also `N/A` for this module.

## Defect regressions fixed by this audit

`PLAT-001`: duplicate `WM_SETFOCUS`/`WM_KILLFOCUS` produced duplicate focus events. Fixed with edge-triggered publication and covered by `TestFocusEventsAreNotDuplicated`.

`PLAT-002`: repeated `WM_SIZE` produced duplicate resize/minimize events and zero client area could be published as a usable resize/restore. Fixed with explicit transition handling and covered by `TestWindowStateTransitionEvents`.

`PLAT-003`: destroying a native window while the runtime held the last `shared_ptr` could release the wrapper inside its own WndProc and leave `GWLP_USERDATA` dangling for later teardown messages. Fixed with WndProc keep-alive plus clearing native userdata before runtime ownership release. Covered by the ownerless-destruction leg of `TestNativeCloseReflectionAndWrapperLifetime`.

`PLAT-004`: a successful `LoadLibraryExW` followed by C++ wrapper allocation/construction failure had no RAII guard for the acquired `HMODULE`. Fixed with `ScopedModuleHandle`; `TestDynamicLibraryContracts` now sweeps every observed C++ allocation boundary of a successful load and proves that injected `std::bad_alloc` never leaves the DLL loaded.

`PLAT-005`: `ReleaseCapture()` could synchronously emit `WM_CAPTURECHANGED` before local capture state was updated, producing duplicate capture-change publication. Fixed by committing local capture state before the native call and covered by `TestMouseCaptureEventsAreNotDuplicated`.

`PLAT-006`: final-window exit observation was a plain boolean and terminal exit still allowed later `CreateWindow()`. Fixed with atomic sticky exit state and explicit `platform.exit_requested` rejection. Covered by `TestWindowLifecycleAndExitRequest`.

`PLAT-007`: window ids were consumed before successful native creation and two runtime indexes did not have an explicit rollback path. Fixed by committing the id only after native creation plus both checked index insertions, with rollback of the first index on second-index failure. The complete observed allocation boundary sweep is covered by `TestWindowCreationAllocationFailureAtomicity`.

`PLAT-008`: a closed wrapper that outlived `WindowsPlatformRuntime` still resolved its dead owner before recognizing that `Show()` had no native window left. `Show()` and `Close()` now return before owner lookup when the native handle is already invalid; `TestWindowWrapperAfterRuntimeTeardown` verifies the surviving wrapper remains inert after runtime teardown.

## Goal 2.5 checklist evidence

The implementation and registered tests cover all eleven Goal 2.5 obligations:

1. Window create/destroy lifecycle: `TestWindowIdBoundaryPolicy`, `TestWindowLifecycleAndExitRequest`, `TestWindowWrapperAfterRuntimeTeardown`, `TestWindowCreationAllocationFailureAtomicity`, `TestNativeCloseReflectionAndWrapperLifetime`.
2. Native close reflection: `TestNativeCloseReflectionAndWrapperLifetime`, `TestPlatformAndInputIntegration`.
3. Resize/minimize/restore/zero area: `TestWindowStateTransitionEvents`.
4. Focus/input event duplication: `TestFocusEventsAreNotDuplicated`, `TestMouseCaptureEventsAreNotDuplicated`, `TestPlatformAndInputIntegration`.
5. Event pump stale replay: `TestEventPumpDoesNotReplayStaleEvents`.
6. High-resolution monotonic clock: `TestPlatformClockIsMonotonic`.
7. Idempotent exit request: `TestWindowLifecycleAndExitRequest`.
8. Dynamic-library load failure atomicity: `TestDynamicLibraryContracts` allocation sweep plus `ScopedModuleHandle` ownership review.
9. Missing symbol controlled failure: `TestDynamicLibraryContracts`.
10. Library/native handle lifetime: `TestDynamicLibraryContracts`.
11. Neutral public contracts: public-header scan, self-containment gate and `TestNeutralPlatformValueContracts`.

## Verification performed in this environment

The changed production `windows_platform_runtime.cpp` compiles as C++20 with Clang `-Wall -Wextra -Wpedantic -Werror` against Win32 declaration stubs. The expanded `platform_input_integration_tests.cpp` also passes strict Clang syntax compilation with the real project public headers. The stubs are audit-only files outside the repository and are not part of the delta.

The architecture, dossier, public API, public-surface and other freeze validators are rerun after generated evidence is synchronized. `TestShutdownRetryAfterExternalCleanupFailure` additionally creates an untracked user of the registered Win32 class, forces `UnregisterClassW` cleanup failure, verifies already-destroyed tracked windows stay destroyed, then removes the external user and verifies shutdown retry succeeds.

Post-merge Windows qualification used MSVC `19.50.35729`, C++20 and `/W4 /WX`. It found and fixed a transient hidden-window sizing defect: one correction based on the first pre-layout client rectangle produced the wrong final client size. `EnsureClientSize` now re-reads and converges the native/client rectangles before ownership publication. Construction-event assertions run before explicit Show/Hide transitions, so real focus messages are not misclassified as construction leakage.

The process-wide `operator new` fault sweep remains enabled on the GCC/Clang configurations that originally established the allocation-failure evidence. It is intentionally not executed under MSVC Debug STL because exception propagation from a replacement allocator can re-enter the debug heap and deadlock independently of Platform. Windows still executes every controlled Win32 failure/retry path, real DLL load/symbol/lifetime checks, and all lifecycle/event assertions.

## Corrective patch 2026-09-18

`PLAT-009`: C++ exceptions from event-vector growth could escape the Win32 callback boundary. `WindowsWindow::HandleMessage` and `StaticWindowProc` are now catch-all callback boundaries. State-changing messages prepare event publication before cached-state commit so an allocation failure can be swallowed without exposing a half transition.

`PLAT-010`: `WM_SIZE`, focus loss/gain and mouse-button/capture handling could commit cached state before a fallible event append, making cached state disagree with the event stream. Each affected message now prepares one atomic event batch first and commits cached state afterward. Native capture calls occur only after successful publication.

`PLAT-011`: `WM_DESTROY` could fail during close-event publication before authoritative runtime/window bookkeeping completed. Native handle invalidation, userdata detachment and runtime index removal now happen before the best-effort close notification.

`PLAT-012`: `CloseNativeWindowNoThrow` and `Impl::~Impl` could still terminate or propagate through cleanup if publication/native teardown failed. Both paths are now genuine no-throw cleanup boundaries with inert fallback state rather than `std::terminate`.

`PLAT-013`: `BuildProcessInfo` manually released the `CommandLineToArgvW` result only on the success path. The Win32 allocation is now owned by a local `unique_ptr` deleter using `LocalFree`, including exceptions from argument conversion/vector growth.

`PLAT-014`: public clock wording could be read as wall-clock semantics even though the implementation uses the Foundation steady-clock boundary. Public comments now state the monotonic steady-clock contract explicitly.

`TestWin32CallbackAllocationFailurePreservesState` injects allocation failure into focus event publication, verifies no exception crosses `SendMessageW`, verifies cached focus/event queue remain unchanged, and verifies a later callback publishes one transition. As with the existing Platform allocation sweeps, this replacement-allocator test is intentionally excluded under MSVC Debug STL and remains evidence for supported non-MSVC fault configurations; real Win32 lifecycle regressions remain in the Windows suite.
