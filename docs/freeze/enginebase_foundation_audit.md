# EngineBase/Foundation local freeze audit

Audit date: 2026-09-17.

Status: `LOCAL_READY` for Goal 2.1. This status is local to `EngineBase/Foundation`; it does not advance the other EngineBase modules or the later system freeze goals.

## Scope and ownership

`EpidemicFoundation` is an `INTERFACE` target with no CMake link dependencies. The production module consists of six public headers: `error.h`, `handle.h`, `path.h`, `result.h`, `string_id.h`, and `time.h`. It owns no service, registry, I/O resource, callback, thread, persistent store, journal, or external backend.

The explicit mutation surface in the generated callable inventory is `FrameIndex` increment. `Result<T>` also has compiler-generated copy/move assignment through its guarded private storage; those special-member semantics are audited in the Result tests even though implicit special members are not separate lexical inventory rows. Other Foundation declarations are value carriers, factories, or queries. `Handle<TTag>` carries identity only; the owning registry remains responsible for comparing the complete index/generation pair against its live slot generation.

Foundation uses `std::filesystem::path` only for lexical path construction, lexical normalization, composition, and string conversion. It does not call filesystem queries or mutations. No Foundation header includes threading or synchronization facilities, Win32 headers, EngineRuntime, or EngineFramework headers.

## Goal 2.1 evidence

### Error

`Error::Create` owns exact copies of code, message, and context. The code is an independent machine-readable field and is not derived from diagnostic text. `TestErrorStableCode` verifies exact code matching, copy/move preservation, and independence from message/context changes.

### Result

`Result<T>` has no default empty state. It contains either `T` or `Error`. Wrong-branch access throws `std::runtime_error`. Move-only payload extraction is supported by the rvalue `Value()` overload. Copy assignment stages a replacement before commit. Move assignment exists only when the internal variant can be move-assigned without throwing, so a payload with a potentially throwing move cannot drive the result into `std::variant::valueless_by_exception`. `Result<void>` stages an `Error` copy before changing its success flag. `TestResultValueErrorMoveAndMisuse` covers value, error, const/non-const access, move construction, copy and move assignment, assignment failure atomicity, move-only payloads, void success/failure, compile-time rejection of unsafe move assignment, and invalid branch access.

### Typed IDs

The single invalid `BasicId<Tag>` representation is raw value `0`. Empty source text maps to `0`; non-empty source text reserves `0` and remaps a theoretical zero hash to `1`. Tag types keep ID spaces distinct at compile time. Equality compares raw values and `std::hash` hashes the same raw value. `TestTypedIdsInvalidStateAndTypeSafety` and `TestHashEqualityConsistency` cover these contracts.

### Handles and stale identity

The canonical invalid handle is `index == kInvalidIndex` and `generation == 0`. Explicit construction with an invalid index canonicalizes the generation to zero, so invalid handles compare and hash identically. Valid handle equality and hashing include generation. `TestHandleGenerationAndCanonicalInvalidState` simulates remove/recreate by advancing the live generation for the same slot and proves that the old handle no longer matches the owner generation.

Generation advancement and exhaustion policy belong to the owning registry. Foundation intentionally does not allocate slots or mutate generations.

### Frame and time values

`FrameIndex::Next`, prefix increment, and postfix increment reject `UINT64_MAX` with `std::overflow_error`; mutating increments leave the pre-state unchanged on overflow. There is no successful no-op increment.

`FrameTime::FromDuration` accepts the complete native `Duration` domain. `FrameTime::FromChrono`, `FromSeconds`, and `FromMilliseconds` reject NaN/infinity with `std::invalid_argument` and reject non-representable conversions with `std::out_of_range` before integral conversion. Native `Duration::min()` and `Duration::max()` roundtrip exactly. Same-period integral conversion has an exact signed/unsigned range check, avoiding dependence on `long double` precision on MSVC where `long double` has `double` precision. `TestFrameTimeConversionsAndBoundaries` and `TestFrameIndexCheckedArithmetic` cover normal, negative, minimum, maximum, signed/unsigned native-period boundary, overflow, underflow-side, and failure-atomicity behavior.

### Paths

`Path::FromString` and `Path::Join` are lexical only. Empty input is valid. `FromString` normalizes dot segments according to native filesystem syntax. `Join` accepts only relative children, normalizes the combined path, treats an empty child as a no-op, and rejects rooted/absolute children so they cannot replace the base. Embedded NUL is rejected by string parsing, joining, and the direct `std::filesystem::path` constructor with `std::invalid_argument`. `TestPathNormalizationAndMalformedInput` covers empty paths, separators, roots, dot segments, rooted child rejection, direct native access, and malformed NUL input through every public construction path.

## Defects fixed during this audit

`FND-001`: `FrameIndex` wrapped from `UINT64_MAX` to `0`. Regression: `TestFrameIndexCheckedArithmetic`.

`FND-002`: floating `FrameTime` conversion passed NaN, infinity, and out-of-range values into integral `duration_cast`, producing invalid boundary behavior. Regression: `TestFrameTimeConversionsAndBoundaries`.

`FND-003`: `Path::Join` accepted rooted children and could silently discard the base path; embedded NUL text was also accepted. Regression: `TestPathNormalizationAndMalformedInput`.

`FND-004`: `Handle(kInvalidIndex, nonzero_generation)` produced multiple structurally different invalid handles. Regression: `TestHandleGenerationAndCanonicalInvalidState`.

`FND-005`: implicit assignment of `Result<T>` delegated directly to `std::variant`. For a payload with a throwing move, branch-changing move assignment could leave the destination `valueless_by_exception`, after which `HasValue()` reported false while `GetError()` threw `std::bad_variant_access`. Assignment now uses private guarded storage: unsafe move assignment is not available, copy assignment stages before a no-throw commit, and `Result<void>` updates its branch flag only after `Error` staging succeeds. Regression: `TestResultValueErrorMoveAndMisuse`.

Additional hardening: a theoretical non-empty string hash of zero would collide with the reserved invalid typed-ID state. `BasicId::FromString` now reserves zero explicitly. This was not a reproduced defect, so it is not entered in the defect ledger. `TestTypedIdsInvalidStateAndTypeSafety` preserves the public invariant that ordinary non-empty IDs are valid and raw zero remains the sole invalid representation.

Self-review also found an evidence defect in `module_dossiers.py`: same-line declarations such as `template <typename T> class Result` and `template <typename Tag> struct BasicId` were omitted from the public type inventory. The parser now recognizes a leading template declaration while still respecting class visibility, and its self-test explicitly requires `Result`/`BasicId` while rejecting private nested `Storage`/`Variant` implementation types.

## LOCAL_READY applicability decisions

Foundation has no lifecycle state, shutdown, cleanup, external transaction, rollback/reconciliation, snapshot/restore, callback/backend boundary, authoritative collection, secondary index, event/revision/journal publication, or persistent state. The corresponding LOCAL_READY criteria are `N/A` with module-specific rationale in `local_ready_ledger.json`.

Single-value mutation atomicity applies to `FrameIndex` and is proven by the overflow tests. Counter boundary behavior applies to `FrameIndex` and handle generations. Empty-state coverage applies to default/empty IDs, handles, paths, `FrameIndex`, and zero `FrameTime`. Duplicate identity coverage applies to deterministic typed-ID creation from the same text. Stale identity coverage applies to generational handles.

## Verification performed

The Foundation test executable contains eight named cases and passes with warnings treated as errors under GCC and Clang in the audit environment. All six public headers also compile independently under both compilers. Goal 1 generated inventories, coverage manifests, dossier checks, public-surface checks, architecture checks, and their negative self-tests are rerun after the Foundation contract changes.

The Windows/MSVC full-project profiles remain the authoritative platform baseline from Goal 1. This archive was audited in a Linux tool environment, so Windows-only Base integration and D3D11 execution are not claimed by this subsection; no Windows-specific production code was changed by Goal 2.1.
