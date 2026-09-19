# EngineRuntime/RuntimeFoundation local freeze audit

Audit date: 2026-09-19.

Status: `LOCAL_READY` for Goal 3.1. This document is module-local evidence only; whole-engine `SYSTEM_READY` and `FROZEN` remain outside Goal 3.

## Scope and ownership

`EpidemicRuntimeFoundation` owns no long-lived runtime service, registry, I/O object, thread, external callback, backend, queue, snapshot store, or cleanup resource. It defines shared Runtime value contracts: typed IDs and handles, monotonic ID helpers, numeric validation, budgets, operation/state enums, time arithmetic, and spatial values/helpers.

The only mutation surface is caller-owned scalar state passed to the monotonic ID helpers and the owner pointer carried by `MonotonicIdReservation`. All other APIs are value construction or queries. Persistence, lifecycle shutdown, external rollback/reconciliation, callback exception containment, journals, derived indexes, and allocation-failure atomicity of containers are therefore not applicable to this module.

## Confirmed defects found during Goal 3.1

### G3-RF-001 — RuntimeFoundation documentation overstated TRS semantics

Problem: the RuntimeFoundation documentation did not state the exact observable approximation used by `ComposeTransform` strongly enough for scale/rotation interaction.

Why it matters: simplified TRS cannot represent shear produced by arbitrary non-uniform parent scale combined with rotated children. Treating `ComposeTransform` as exact affine matrix composition would create a stronger contract than the implementation provides.

Cause: the documentation described the hierarchy restriction without fixing the precise deterministic approximate representation as the contract.

Fix: document that `ComposeTransform` deterministically computes parent-transformed local position, normalized quaternion multiplication, and component-wise scale multiplication. It does not promise shear preservation, exact general affine composition, or a decomposition API. Keep the existing value model and add regression coverage for repeated deterministic composition, negative scale, and the documented tolerance.

Regression/evidence: `TestApproximateTrsCompositionContract`, `TestQuaternionNormalizationAndFallbackContract`, and `TestTransformAabbNegativeScaleAndDegenerateBounds` in `runtime_foundation_tests.cpp`.

### G3-RF-002 — upper-bound seconds conversion can execute an out-of-range float-to-int cast

Problem: `CheckedSecondsToMicroseconds()` computed `seconds * 1'000'000` in `double` and compared it with `static_cast<double>(INT64_MAX)`.

Why it matters: `INT64_MAX` rounds to `2^63` when represented as `double`. A valid input close to the upper limit can therefore produce a rounded `double` value of `2^63`, pass the `>` check, and then reach an out-of-range conversion to `std::int64_t`. On the audit compiler the old implementation returned `INT64_MIN`; UBSan reports `float-cast-overflow`.

Cause: range validation was performed in the same floating representation that cannot distinguish `INT64_MAX` from the next integer power-of-two boundary.

Fix: perform the multiplication and range comparison in `long double`, comparing against an exact `long double` conversion of `INT64_MAX`, and only cast after the value is proven representable.

Regression/evidence: `TestCheckedSecondsToMicrosecondsBoundaries` exercises the rounded upper-bound input and its next representable overflow input. The former must produce a non-negative representable microsecond count, and the latter must be rejected. The same path is run under UBSan in the local audit.

### G3-RF-003 — `spatial.h` is not self-contained

Problem: `TransformAabb()` uses `std::size(corners)`, but `spatial.h` does not include the standard header that declares `std::size`.

Why it matters: a consumer that includes `spatial.h` directly fails to compile even though the public-header freeze contract requires every public header to be independently consumable with its declared dependencies. The aggregate `runtime_foundation.h` masked the defect through transitive includes in the module test.

Cause: the implementation relied on an incidental transitive declaration of `std::size` rather than including `<iterator>` directly.

Fix: include `<iterator>` in `spatial.h`; do not alter the spatial API or implementation semantics.

Regression/evidence: compile every RuntimeFoundation public header as the only project include in a translation unit under GCC and Clang with warnings treated as errors. `spatial.h` must compile independently.

## Audit decisions

Runtime numeric IDs reserve raw value `0` as the single invalid state. `AssetId` and `ResourceId` wrap `foundation::StringId`, whose single invalid state is also raw `0`; the two remain distinct C++ types even when created from identical source text. All numeric Runtime ID families are distinct tag-derived types and are not implicitly constructible from each other.

Monotonic allocation is checked at `1`, ordinary values, `UINT64_MAX`, and exhausted `0`. Peek and reserve do not consume the counter. Rollback does not consume it. Commit advances only when the live counter still equals the reserved value, so repeated or stale commits are no-ops. Allocation of `UINT64_MAX` moves the counter to exhausted `0` without wrap-around.

`RuntimeBudget{}` and zero item/byte/time fields mean unlimited. Negative time is invalid. `RuntimeBudget` is a value contract only; each consuming major defines whether its particular limit is hard, soft for the current work unit, deferred, or backpressure. Maximum unsigned item/byte values remain unsigned through current Runtime consumers and are not converted through a narrower signed type.

Time conversion rejects negative, NaN, infinity, and non-representable seconds/scales. Checked game-time arithmetic covers `INT64_MIN/MAX`, including `INT64_MIN` duration, before any signed arithmetic that could overflow. Convenience operators use the documented saturating direction.

Spatial finite validation covers `Vec3`, `Quat`, `Transform`, and `Aabb`. `Normalize()` returns identity for zero-length or non-finite/overflowed quaternion magnitude; that fallback is a convenience behavior and is not authoritative input validation. Authoritative callers must use `IsFinite`, `IsNormalized`, and `IsValidTransform` as appropriate. `IsValidTransform` requires finite fields, normalized rotation, and non-zero scale on all axes. `IsValidAabb` rejects inverted bounds. `Sphere` has no common validator because the only authoritative Runtime query consumer currently validates center/radius at its own public boundary.

## Failure atomicity and observable state

The module has no module-owned collection or durable primary state. Its only observable mutation is advancement of a caller-owned monotonic counter. `PeekMonotonicId` and `ReserveMonotonicId` are non-consuming; `Rollback` is non-consuming; `Commit` changes the counter only when the reserved value still matches the live counter; exhausted allocation fails without changing `0`. The result/error construction on exhausted paths occurs before any counter mutation. There is no multi-container commit, external side effect, callback, or reconciliation path in RuntimeFoundation.

The time and spatial helpers are pure value operations. Checked operations return failure before producing an authoritative value; saturating operations return a defined boundary value. `Normalize` has a documented identity fallback and is not a validation API.

## LOCAL_READY applicability

RuntimeFoundation has no owned lifecycle, shutdown path, persistent checkpoint, callback/backend boundary, external side effects, multi-container mutation, derived index, cleanup ownership, journal, event publication, or long-lived authoritative collection. Those LOCAL_READY criteria are `N/A` for this module with the module-specific rationale above.

The applicable criteria are public contract classification, invalid/boundary behavior, scalar mutation atomicity, counter exhaustion, semantic no-op behavior, empty/default values, type-safe identity, and deterministic spatial/time value semantics.


## Verification

The production top-level CMake configuration cannot complete in this Linux audit environment because the project intentionally rejects non-Windows platform builds in `EngineBase/Platform`. No Windows/MSVC whole-project qualification is claimed here. Before any build attempt the complete extracted project tree was copied to a separate task backup directory and checksummed.

The actual `EpidemicRuntimeFoundation` and `EpidemicRuntimeFoundationTests` CMake targets were built in an isolated harness with the real module CMakeLists and warnings treated as errors. Debug and Release both pass `EpidemicRuntimeFoundationTests`. The same sources/tests pass direct GCC 14.2 and Clang 17 Debug/Release builds. GCC UBSan with `undefined,float-cast-overflow` passes the complete module suite.

All ten RuntimeFoundation public headers compile independently as the sole project include under GCC and Clang with warnings treated as errors. This permanently corresponds to the repository `EpidemicPublicHeaderSelfContainment` contract used by the normal Windows qualification.

The architecture ownership, exact public API inventory, public surface manifest, module dossier, quantified coverage manifest, LOCAL_READY contract, semantic CI gate, and exact CTest manifest checks and their available negative/self-tests pass on the resulting tree. The LOCAL_READY ledger reports EngineRuntime/RuntimeFoundation as the first Runtime module admitted after the nine EngineBase modules.
