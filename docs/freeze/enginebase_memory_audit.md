# EngineBase/Memory local freeze audit

Audit scope: Goal 2.2 from `Epidemic_Engine_Full_Freeze_Plan_2026-09-14.md`.

Module: `EngineBase/Memory`

Target: `EpidemicMemory`

Result: `LOCAL_READY` for module-local correctness. System-wide lifetime/concurrency/load qualification remains governed by Goals 6-9.

## Reviewed ownership and state

Authoritative state is limited to `MemoryTracker::tracking_enabled_`, per-tag `statistics_`, and per-tag `budgets_`. `TrackingAllocator` owns no allocations and stores non-owning references to the tracker and upstream allocator. `DefaultAllocator` has no mutable owner state. There are no indexes, IDs, generations, revisions, cursors, journals, events, persistence records, callbacks, or external transaction state.

`MemoryTracker` serializes all authoritative state with `mutex_`. Allocation tags are normalized before indexing. Current bytes and allocation count use saturating arithmetic. Free accounting clamps at zero. Peak is monotonic between explicit statistics resets. `ResetStatistics()` retains current live bytes and budgets while restarting historical count and peak.

The module has no persistent state and no shutdown lifecycle. `IFrameAllocator` is an abstract extension point only; there is no production frame allocator whose storage lifetime can be exercised. Its reviewed contract makes `Reset()` the explicit lifetime boundary, and concrete frame allocator lifetime testing is therefore not applicable until an implementation is added.

## Mutation contract inventory

| Callable IDs | Contract |
|---|---|
| `ca2661ba30be7a8b`, `754e6b9407d3bf77`, `253b30bd6ed1e09f` | `Allocate`: zero size normalizes to one byte; size greater than `PTRDIFF_MAX` is rejected; alignment must be a non-zero power of two. Success returns storage satisfying alignment. `DefaultAllocator` may throw `std::bad_alloc`; `TrackingAllocator` propagates upstream exception or `nullptr` and records accounting only after a non-null result. Rejected/failed allocation leaves tracker state unchanged. |
| `9c6bfd03ec85c1d2`, `972fb0dbb7a56234`, `4111a0fbef8d4176` | `Deallocate`: pointer/size/alignment/tag must describe the original allocation. `nullptr`, size greater than `PTRDIFF_MAX`, zero alignment, or non-power-of-two alignment is a controlled no-op. Valid `TrackingAllocator` deallocation reaches upstream once and records one free. |
| `36023fcf1795b76c`, `de7030761c598078` | `SetTrackingEnabled`: sets the mode for subsequent accounting calls. Repeating the current value is a no-op. The call is noexcept and does not reconstruct history. Exact live accounting therefore requires callers not to disable tracking across a matched record pair. |
| `5fef52c01590b43a`, `c3116f4062aef3df` | `RecordAllocate`: disabled tracker or zero bytes is a no-op. Known and invalid tags are isolated through normalization. Current bytes and allocation count saturate instead of wrapping; peak becomes max(previous peak, current bytes). |
| `ae68b32e5929f68b`, `da9fe3d568c3cf87` | `RecordFree`: disabled tracker or zero bytes is a no-op. Valid accounting subtracts bytes. Over-free clamps current usage to zero and never changes peak or allocation count. |
| `b6f7b28d68314a6f`, `dbcfc6ad73f0a71e` | `SetBudget`: sets/replaces a soft per-tag budget. Repeating the same value is a semantic no-op. Budget does not mutate usage/statistics. `IsOverBudget` is false at `usage == budget` and true only above it. |
| `514c90d19098afc3`, `3f2a40c0fa2bbffd` | `ResetStatistics`: preserves live `allocated_bytes` and budgets, sets peak to current live bytes, and resets historical `allocation_count` to zero. Repeating reset without intervening accounting is idempotent. |
| `fbec9ab4fb40f82a` | `IFrameAllocator::Reset`: abstract lifetime boundary. A conforming implementation invalidates the prior frame/batch lifetime only on explicit `Reset`. EngineBase currently ships no concrete frame allocator. |

## Query and value contract inventory

| Callable IDs | Contract |
|---|---|
| `357f8b4de0ea63ae`, `1c5749a2cf752b32`, `d7f493dd16490d18`, `15de3466429b4e1d` | Allocation tag helpers expose the fixed valid range, reject out-of-range values from the known set, normalize invalid values to `Unknown`, and provide stable diagnostic names. |
| `6fad43fe89031df2` | `NormalizeAllocationSize` maps zero to one and leaves every non-zero representable size unchanged. |
| `087bbf32fc17be10`, `8f96c3f5a195ecd3`, `d83ffe9c464c3bde` | Interface destructors are virtual and permit destruction through the interface without owning additional state. |
| `2ba9527806fc0f7e` | `TrackingAllocator` construction binds non-owning tracker/upstream references and performs no allocation/accounting side effect. |
| `84ec87c765c5d4ef` | `MemoryTracker` construction initializes zero statistics/budgets and the requested tracking-enabled state. |
| `caf39af769cfcc97`, `e8312ce1c05d32ba` | `IsTrackingEnabled` returns the mutex-protected current mode. |
| `7b5596491888e107`, `a4009d39c27185ea` | `GetBudget` returns a detached optional value for the normalized tag. |
| `15f35fc3d33ead40`, `1468ab6b5803dbc9` | `GetUsage` returns current tracked bytes for the normalized tag. |
| `469f40e17dafae5f`, `227b5f5ce051530b` | `IsOverBudget` is false without a budget and otherwise uses strict `usage > budget`. |
| `c6ab955074611085`, `bb1d6a88fa04c731` | `GetStatistics` returns a detached statistics value under the tracker mutex. |

There are no `UNCLASSIFIED` Memory rows. Each interface declaration, concrete override, constructor, destructor and query remains a separate signature-level contract in `docs/freeze/public_api_inventory.md`.

## Goal 2.2 evidence

1. Normal allocation/deallocation and alignment are checked by `AllocationContractAndAlignment` and `TrackingAllocatorExactlyOnce`.
2. Invalid alignment and unrepresentable size are checked by `InvalidAllocationArguments`; rejected deallocation is a controlled no-op.
3. Exactly-once tracking is checked for successful allocate/free and null no-op paths.
4. Throwing OOM and upstream `nullptr` leave current usage and allocation count unchanged in `AllocationFailureAtomicity`.
5. Deallocation returns usage to the expected value and protects against underflow in `TrackingAllocatorExactlyOnce` and `BudgetsPeakAndReset`.
6. Peak remains unchanged by free; explicit statistics reset restarts peak from current live usage.
7. Budget behavior below, at, and above the boundary is checked by `BudgetsPeakAndReset`; maximum-size boundary comparison is checked by `CounterOverflowDoesNotWrap`.
8. Allocation tags remain isolated, and invalid tags map only to `Unknown`, in `AllocationTagIsolationAndInvalidTag`.
9. Frame reset production behavior is not applicable because only `IFrameAllocator` exists. The interface contract is explicit and a probe verifies that the lifetime boundary occurs only when `Reset()` is invoked.
10. Byte arithmetic is runtime-tested at `SIZE_MAX`; allocation-count saturation and byte saturation also have compile-time boundary assertions in `memory_tracker.cpp`.
11. OOM and rejected allocation paths do not publish tracker mutations.

## Defects found and fixed during the audit

`MEM-001`: `RecordAllocate` used unchecked `size_t` addition/increment, allowing usage/peak/allocation count wrap-around. Fixed with saturating arithmetic and boundary regression evidence.

`MEM-002`: `TrackingAllocator` recorded an allocation even when a non-conforming/fallible upstream returned `nullptr`. Fixed by publishing accounting only after a non-null result.

`MEM-003`: `ResetStatistics()` cleared current live usage. This could make subsequent budgets and frees inconsistent while allocations remained alive. Fixed by retaining current bytes and budgets, resetting only historical count/peak window.

`MEM-004`: invalid alignment reached aligned global new/delete without a deterministic public contract. Invalid alignment and unrepresentable object size are now validated before upstream/global allocation; invalid deallocation metadata is a safe no-op.

## Regression scope

The dedicated test executable is `EpidemicMemoryUnitTests`. The source is also compiled and executed standalone under GCC with C++20, `-Wall -Wextra -Wpedantic -Werror` as a portability check. The repository-wide Windows CMake/CTest regression remains the authoritative platform gate because the EngineBase platform slice intentionally refuses non-Windows top-level configuration.
