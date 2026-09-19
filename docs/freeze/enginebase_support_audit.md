# EngineBase Support local freeze audit

Audit scope: `EngineBase/Support` for Goal 2.9. Status after this audit: `LOCAL_READY`, verified by the repository-wide Windows/MSVC Debug and Release regression matrix.

## Responsibility and ownership

Support is a composition layer. It creates and wires EngineBase services into a caller-owned `core::Application`; it owns no independent authoritative gameplay/runtime state and it does not expose a process-global service registry. Service lifetime remains scoped to the `Application::Services()` container. Frame callbacks capture only the service objects they need.

Composition is owner-thread/external-serialization work. Concurrent registration is not part of the Support contract. `ServiceContainer` still protects its own map, and the new `RegisterInstancesAtomic` primitive supplies all-or-nothing publication for a preconstructed service bundle.

## Public composition contracts

`EnsureFramePlatformEvents` returns the existing application-scoped frame buffer or registers exactly one new buffer. A sealed container without an existing buffer rejects registration and leaves the service graph unchanged.

`RegisterEngineBase` validates the full baseline service bundle before construction and immediately before publication. `worker_count == 0` normalizes to one consistently in both configuration and scheduler. Worker count and default window dimensions are range checked before publication. The six baseline services are published through one atomic container operation. Existing services are never replaced.

`RegisterWindowsRuntime` treats `IPlatformRuntime` and `IWindowSystem` as one registration bundle. A conflict in either role rejects the helper before either role is published. Both interfaces reference the same concrete runtime object.

`RegisterInputRuntime` rejects duplicate or sealed registration before replacing anything.

`RegisterGraphicsRuntime` accepts only `Null` and `D3D11`. Unknown enum values return `engine_base.support.invalid_graphics_backend`. Device and command context are constructed before service publication and then registered as one atomic bundle. Factory, command-context, duplicate, sealed, and publication failures are controlled `Result` failures. Both backends expose the same `IRhiDevice` plus `IRhiCommandContext` service contract.

`CreateMainWindow` requires a registered `IWindowSystem`; missing composition is a controlled `Result` failure. Platform-specific create failures remain the window-system result and do not create a Support owner.

`RegisterMainSwapChain` rejects null windows, invalid native handles, missing device, duplicate/sealed ownership, and backend create failures as controlled `Result` failures. Duplicate ownership is checked before backend work. `ServiceContainer::RegisterInstanceFromFactoryAtomic` reserves the service-map node before invoking `IRhiDevice::CreateSwapChain`; after a successful backend creation it fills that prepared node and swaps the candidate map without another allocation. Thus the device's active swap chain and application service owner commit as one operation.

`RegisterPlatformFrameLoop` and `RegisterInputFrameLoop` resolve required services before creating the shared `FramePlatformEvents` buffer. Missing dependencies therefore do not leave a partially composed frame-event service. `RegisterRhiFrameLoop` validates every supplied shared service/state pointer before installing any frame handler.

`RegisterFrameThrottle` installs only an end-of-frame pacing callback. Zero duration is a valid no-op sleep duration; negative duration is accepted by `sleep_for` as an immediate return and does not mutate authoritative engine state.

The exact public inventory contains eleven Support callables and no overloaded Support signature, so overload collapse is not applicable; each callable remains a separate inventory contract row.

## Success and no-op semantics

Successful registration preserves previously registered unrelated services. Repeated `EnsureFramePlatformEvents` is idempotent and returns the same buffer. Registration helpers that represent unique service ownership reject duplicates rather than silently no-op or replace an owner. Frame-loop helpers are one-time composition operations by contract; duplicate handler installation is a caller composition error and is not used as an idempotent API.

## Failure atomicity

The previous sequential service publication allowed late conflicts to leave an incomplete bundle. Support now preconstructs candidate services and uses `ServiceContainer::RegisterInstancesAtomic`, which copies the live service map under the container lock, inserts the complete candidate bundle, and swaps it into live state only after all insertions succeed. Duplicate, sealed, null-instance, and candidate-allocation failures therefore preserve the pre-call service map.

Graphics factories execute before their fully prepared service-bundle publication. Swap-chain creation is the external transaction: the service slot is prepared first and its no-throw commit follows a successful backend create. A failed factory does not publish a Support service, and no fallible service-map operation remains after the backend becomes active. Frame-loop dependency validation occurs before frame buffer or handler publication on the audited failure paths.

Support needs no rollback journal for this path because the only external commit is followed solely by no-throw service publication; it is not an `N/A` classification.

## Lifecycle contract

Composition is valid before the `ServiceContainer` is sealed by application initialization. Service registration after sealing is rejected. Support owns neither application shutdown nor retryable backend cleanup; shutdown responsibility remains with the concrete service owners and Application/Core lifecycle. Support registers callbacks and services but does not create a second lifecycle state machine.

## Persistence and identity applicability

Support owns no persistent state, snapshot schema, ID space, generation, revision, cursor, removable handle, index, or tombstone. Persistence, stale identity, counter-boundary, and restore criteria are therefore not applicable to this module.

## Defects fixed by this audit

1. A late `RegisterEngineBase` service conflict could leave earlier baseline services registered. Fixed by bundle preflight plus atomic publication.
2. A late `RegisterWindowsRuntime` conflict could publish only one of its two service roles. Fixed by atomic two-role publication.
3. A late `RegisterGraphicsRuntime` conflict could publish a device without its command context. Fixed by atomic two-role publication and controlled registration errors.
4. An unknown `GraphicsBackend` enum silently selected Null. Fixed by explicit enum switch and controlled invalid-backend error.
5. `RegisterPlatformFrameLoop` and `RegisterInputFrameLoop` could create `FramePlatformEvents` before discovering a missing required service. Required services are now resolved first.
6. `worker_count == 0` produced scheduler worker count one while configuration still reported zero. Both now report one.
7. `RegisterMainSwapChain` could perform backend work before discovering duplicate ownership, and null/missing composition paths could escape as exceptions. Preconditions are now checked before backend work and reported through `Result`.
8. `RegisterRhiFrameLoop` could install an early handler before discovering a null later dependency. All dependencies are now validated before handler registration.
9. `RegisterMainSwapChain` could activate the RHI device's swap chain before a later `RegisterInstance` allocation failed. Fixed by `RegisterInstanceFromFactoryAtomic`, which reserves the candidate slot before external creation and commits without allocation afterward.

## Goal 2.9 checklist evidence

Bundle atomicity is covered by `TestEngineBaseBundleRejectsLateConflictWithoutPartialRegistration`, `TestWindowsAndInputDuplicateRegistrationAreControlled`, `TestGraphicsRegistrationConflictsAndInvalidBackendAreResultFailures`, and `TestMainSwapChainDuplicateOwnerIsRejectedBeforeBackendWork`.

Valid composition of baseline, Windows, Input, and Null graphics is covered by `TestValidWindowsInputAndNullGraphicsComposition`. D3D11 retains the same `IRhiDevice` and `IRhiCommandContext` publication path, with the existing Windows-only `TestD3D11GraphicsRuntimeFailurePath` and `TestD3D11PositiveSmoke` providing backend-specific coverage.

Unique main swap-chain ownership is covered by `TestMainSwapChainDuplicateOwnerIsRejectedBeforeBackendWork`. Frame phase order is fixed by `TestFrameHandlerOrderIsPlatformInputThenPresentation`: platform pump/drain, input queue/publish, RHI begin/clear, RHI end, present. Missing frame-loop dependencies and invalid RHI wiring are covered independently.

Application scoping is covered by `TestCompositionServicesAreApplicationScoped`, which composes two applications and proves that their logger, event bus, and configuration instances do not alias.

## Verification performed in this audit

The dedicated Support suite passes on the Windows/MSVC full-debug gate. The production Support, Core, Input, RHI Null, Diagnostics, and Memory sources are exercised from the tree.

Repository freeze validators are rerun after evidence regeneration. With the four unattended smoke applications registered in CTest, the exact configured counts are Base `14`, Runtime `34`, Full `94` for both Debug and Release. Historical Goal 1 run counts remain historical evidence and are not rewritten.

On 2026-09-18, the local Windows/MSVC Full Debug CTest run passed 94/94, including `EpidemicSupportIntegrationTests` and the four registered unattended smoke applications; the D3D11 smoke completed with a hidden native window and a one-frame limit.

## Corrective patch 2026-09-18

`SUPPORT-009`: lazy `FramePlatformEvents` registration and frame-handler installation were separate commits. Support now routes Platform/Input frame-loop wiring through one shared composition transaction: the service candidate is staged, the complete handler batch is committed with Core's strong guarantee, then the service map is published by no-allocation swap. `RegisterEngineBase` uses the same pattern for its six baseline services plus the memory end-frame handler.

`SUPPORT-010`: RHI frame-loop registration installed three handlers separately. It now validates and publishes the complete handler batch atomically. If `BeginFrame` succeeds and `Clear` fails, the callback closes the active frame with `EndFrame` before rethrowing the original clear failure. `TestRhiClearFailureClosesActiveFrame` verifies inactive post-state and the exact `begin, clear, end` trace with no present.

`SUPPORT-011`: aggregate memory diagnostics cast and added `size_t` counters directly into signed `int64_t`, permitting overflow in diagnostic publication. Each source value and aggregate addition now saturates at `INT64_MAX`.

`SUPPORT-012`: logger sink exceptions after successful service or graphics/swap-chain publication could make a committed composition appear to have failed. Post-commit informational logging is now explicitly non-authoritative and cannot change the helper's success/failure result.

The common transaction is backed by the new Core contracts `ServiceContainer::RegisterInstancesAtomicWithPreCommit` and `Application::AddFramePhaseHandlersAtomic`; the generated callable/coverage inventories and explicit evidence anchors were regenerated for those two public rows.
