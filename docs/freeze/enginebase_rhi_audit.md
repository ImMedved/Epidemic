# EngineBase/RHI local freeze audit

Audit scope: Goal 2.7 from `Epidemic_Engine_Full_Freeze_Plan_2026-09-14.md`.

Module: `EngineBase/RHI`

Target: `EpidemicRHI`

Result: `LOCAL_READY` for module-local correctness. D3D11 backend qualification remains Goal 2.8; system-wide lifetime/concurrency/load qualification remains Goals 6-9.

## Reviewed ownership and state

The module owns only transient backend-neutral RHI object state. `NullRhiDeviceState` contains a weak binding to the last successfully created live null swap chain. `NullRhiSwapChain` owns its detached descriptor state. `NullRhiCommandContext` owns its `frame_active_` flag and a shared reference to the null device state. `PresentationSurfaceHandle` is an opaque borrowed native pointer value, not an engine ID/generation space.

There are no secondary indexes, revisions, journals, events, persistence records, callbacks, external transactions, retries, or fallible cleanup operations in `EngineBase/RHI`. Mutation is externally serialized until Goal 6 qualifies a stronger threading contract.

## Mutation contract inventory

| Callable IDs | Contract |
|---|---|
| `37443767888118f6` | `BeginFrame`: succeeds only from inactive and makes the context active. Nested begin returns `rhi.frame_already_active` and preserves the original frame. Allocation/backend work is not performed. |
| `bc8946f98056de10` | `Clear`: requires an active frame, a valid `RhiClearDesc`, and a live active presentation target. Failure preserves frame state. Null backend returns `rhi.no_swap_chain` when the target is absent/stale. |
| `b4cc3e4171314c3a` | `EndFrame`: succeeds only from active and makes the context inactive. Repeated/end-without-begin returns `rhi.no_active_frame` without changing state. |
| `9f63de09feb8be08` | `CreateCommandContext`: creates a fresh inactive context bound to device presentation state. Operational allocation failure may propagate `std::bad_alloc`; no device-owned observable state is published before success. |
| `80cd4a4a3be482d8` | `CreateSwapChain`: validates the full descriptor before construction. A successful object becomes the active presentation target only after creation succeeds. Rejected creation leaves any previous active target unchanged. Allocation failure may propagate `std::bad_alloc` and likewise leaves the prior binding unchanged. |
| `1c6ceb666ddb04cc` | `Present`: presents the current live swap chain. In Null RHI it is a successful operation before and after valid resize and has no hidden counter/state publication. |
| `f6b2d326ab93f8d8` | `Resize`: requires non-zero width and height. Success atomically publishes both dimensions. Same-size resize is an allowed semantic no-op. Zero-area resize is rejected with `rhi.invalid_swap_chain_size` and preserves the previous usable dimensions. |

## Query, factory and value contract inventory

| Callable IDs | Contract |
|---|---|
| `b34aeff0241c48bb`, `967d55e46232affe`, `f72c54b2b14d6c5a` | Descriptor validation is side-effect free. Device names must be non-empty; swap-chain surface/dimensions/buffer count/format must be valid; clear operations require a requested color clear and finite color components. |
| `5071afb4f3b0efbe` | `CreateNullRhiDevice` validates first and publishes a device only after construction. Invalid input is a `Result` failure; allocation failure follows the cross-engine `std::bad_alloc` policy. |
| `7f1f8ce7c5fbe42a`, `9b1bd7e2d54da334`, `5e3afaa20434e750`, `882076cf3231ba5c`, `2930c54a37cae55d`, `d7c3fe32704032a4`, `8b5b4b468d672068` | RHI queries expose current logical state or immutable/detached descriptor values without mutation. |
| `4d67429c8061456d` | `ToString` returns stable names for known formats and `Unknown` for both `Unknown` and out-of-range enum values. |
| `b809eb09893b3870`, `9175d1165a88bcb8`, `a89c9edba3de6326`, `49a8698767a41b10`, `c39ed1be31b56f58`, `52fb57b79fe75b84` | `PresentationSurfaceHandle` is a nullable opaque pointer wrapper; construction/casts preserve the pointer, validity is `value != nullptr`, and equality compares wrapped pointer identity. |
| `dbe6d9bcaee914eb`, `2b5e5aecc8ed8fc2`, `96b4276d83e48ff7` | Interface destruction is virtual. Object-owned transient state dies with the object; Null device state keeps only a weak swap-chain binding and therefore cannot retain a destroyed target. |

There are no `UNCLASSIFIED` RHI rows. All 28 exact public callables remain separate signature-level contracts in the API inventory.

## Lifecycle and failure atomicity evidence

`CommandContextLifecycle` proves inactive -> active -> inactive, both forbidden transitions, invalid clear behavior, and preservation of active/inactive state after failure.

`SwapChainPresentResizeLifecycle` proves present before resize, present after resize, same-size no-op, zero-width/height/area rejection, preservation of the prior width/height after failed resize, and continued presentation after rejected minimized/zero-area input.

`DescriptorValidationAndCreationAtomicity` proves invalid device/swap-chain descriptors, including an out-of-range enum, and verifies that a failed replacement swap-chain creation preserves the previously active presentation target.

`CreationAllocationFailureAtomicity` sweeps every observed allocation boundary in null-device, command-context and swap-chain creation. Every injected `std::bad_alloc` starts from a fresh fixture; failed replacement creation preserves the original descriptor and active presentation target.

`NullPresentationTargetLifetime` proves that Null RHI does not hide missing presentation state, does not retain a destroyed swap chain, can replace a stale target, does not leak command-frame state across context destruction, and leaves already-created shared child objects self-contained after device-wrapper destruction.

## Goal 2.7 evidence

1. Device/context/swap-chain creation failures are atomic for controlled input failures and every observed allocation boundary. Allocation failure propagates as `std::bad_alloc`; the fault sweep verifies no partial active-target publication.
2. A second `BeginFrame` fails with `rhi.frame_already_active` and leaves the original frame active.
3. `EndFrame` without an active frame fails with `rhi.no_active_frame`.
4. `Clear` outside a frame fails with `rhi.clear_outside_frame`; malformed clear payload and absent/stale presentation target are also controlled failures that preserve frame state.
5. Null `Present` succeeds before resize and after successful resize. Rejected resize leaves the prior presentation state usable.
6. Zero dimensions are rejected explicitly. The old usable dimensions remain unchanged, so minimized/zero-area input cannot publish an invalid swap-chain size.
7. Null RHI now enforces the same baseline lifecycle shape expected by hardware backends: clear requires both an active command frame and a live presentation target.
8. Active presentation binding is weak, so swap-chain destruction cannot leave a stale target. Command-frame state is object-local and cannot leak into replacement contexts. Device-wrapper destruction does not dangle already-created shared child objects.

## Defects found and fixed during the audit

`RHI-001`: Null RHI accepted `Clear()` inside an active frame even when no swap chain had ever been created. This could make headless/reference tests pass while a hardware backend rejected the same lifecycle. Fixed by introducing shared Null presentation state with a weak active-swap-chain binding and by requiring a live target for `Clear()`.

`RHI-002`: `Validate(RhiSwapChainDesc)` rejected only `RhiPixelFormat::Unknown` and accepted arbitrary out-of-range enum values. Fixed with an exhaustive supported-format switch whose default path returns `rhi.invalid_color_format`.

## Audit debt removed

The Null backend carried unused `clear_count_`, `last_clear_desc_`, `present_count_`, and `resize_count_` fields. They had no public contract and unnecessarily enlarged the authoritative-state surface. They were removed so Null RHI stores only lifecycle/presentation state required by the contract.

## Regression scope

The dedicated CTest executable is `EpidemicRhiUnitTests`. It contains descriptor/value tests, lifecycle tests and a fresh-fixture allocation fault sweep. The RHI source and tests are additionally compiled standalone with C++20 warnings-as-errors under GCC and Clang in this audit environment. AddressSanitizer/UndefinedBehaviorSanitizer are used on the same headless slice. The authoritative Windows Base matrix and D3D11 smoke remain platform gates for integration and Goal 2.8.
