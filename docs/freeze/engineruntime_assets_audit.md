# EngineRuntime/Assets local freeze audit

Audit date: 2026-09-19.

Status: `LOCAL_READY candidate` pending the required MSVC Debug/Release requalification after the Block C fault-harness rewrite. This remains module-local evidence only. Whole-engine `SYSTEM_READY` and `FROZEN` remain outside Goal 3.

## Scope and ownership

`EpidemicRuntimeAssets` owns one in-memory authoritative metadata catalog and its monotonic mutable-to-sealed lifecycle. The primary state is the `AssetId -> AssetMetadata` map plus the `sealed_` flag. Asset metadata contains caller-supplied per-asset version, logical location, state, tags, dependency metadata, and content hash. The module does not own loaded resource payloads, resource residency, filesystem/package I/O, backend handles, threads, callbacks, journals, generated revisions, secondary indexes, or cleanup queues.

Public reads return detached value copies. `IAssetLocationResolver` returns the logical `AssetLocation` stored in the catalog; it does not resolve a host path or perform I/O. `IAssetCatalogWriter` has only `RegisterAsset` and `Seal`; there is no update, replace, erase, restore, or unseal API.

## Confirmed defects found during Goal 3.5

### G3-ASSET-001 — dependency manifest requiredness depended on traversal order

Problem: `BuildDependencyManifest()` deduplicated dependencies by inserting the first observed `AssetDependency` into the output. A shared dependency reachable through both optional and required edges therefore inherited the required flag from whichever path happened to be visited first.

Why it matters: registration order and DFS traversal shape are not semantic inputs. Two equivalent dependency graphs could produce different manifests, and an earlier optional edge could incorrectly weaken a later required edge.

Cause: deduplication stored only an `AssetId` membership set and retained the first complete edge object instead of merging requiredness across all reachable edges.

Fix: accumulate a private `AssetId -> required` map and merge requiredness with logical OR. Materialize the detached manifest only after traversal and sort it by `AssetId::Raw()`.

Regression/evidence: `TestDependencyManifestMergesRequirednessIndependentlyOfTraversalOrder` builds equivalent diamond graphs with opposite root dependency order and requires the shared dependency to remain required in both results. The baseline probe recorded `shared_required_left_first=0` and `shared_required_right_first=1`; the fixed test requires equality and `required == true`.

### G3-ASSET-002 — embedded NUL was accepted inside logical asset paths

Problem: path validation accepted `std::string` values containing an embedded `\0` byte as long as their visible prefix satisfied the relative-path checks.

Why it matters: downstream C/C++ filesystem or package boundaries may interpret the same string through a NUL-terminated API and observe only the prefix. A value such as `safe.asset\0../../escape.asset` would therefore have two different path meanings across boundaries.

Cause: `IsSafeRelativePath()` validated separators, absolute forms, and `..` depth but did not reject embedded NUL bytes.

Fix: reject any path containing `\0` before absolute/traversal processing. The existing canonicalization contract remains lexical and engine-root-relative.

Regression/evidence: `TestPathValidationRejectsTraversalAndRequiresMounts` includes an embedded-NUL path and requires `asset.invalid_location` with no catalog publication. The baseline probe recorded `nul_path=accepted`.

### G3-ASSET-003 — invalid Resolve ID was reported as ordinary missing asset

Problem: `Resolve(AssetId{})` delegated to `FindById()` and returned `asset.not_found`, so malformed invalid identity and a valid but absent identity had the same Result failure.

Why it matters: the Goal 3 contract requires invalid IDs and missing registered objects to be rejected unambiguously. Callers otherwise cannot distinguish a precondition violation from an ordinary lookup miss.

Cause: the resolver had no explicit invalid-ID boundary check and used the optional query as its only lookup path.

Fix: validate `id.IsValid()` before lookup and return `asset.invalid_id`; keep `asset.not_found` for valid but absent IDs. `FindById`, `Contains`, `FindByType`, and `FindByTag` document their non-Result invalid-selector behavior as empty/not-contained.

Regression/evidence: `TestInvalidQueryInputsAreControlled` verifies empty/not-contained behavior for non-Result queries and distinct `asset.invalid_id` failures for `Resolve` and `BuildDependencyManifest`. The baseline probe recorded `invalid_resolve=asset.not_found`.

## Registration and validation audit

Registration rejects invalid `AssetId`, `AssetType`, `AssetState`, and out-of-domain `AssetLocationKind`. Version zero is rejected. Version is caller-supplied per-asset metadata/content version and is not a generated global catalog revision.

Logical paths are bounded by `kMaxAssetPathBytes`, must be non-empty, must not contain embedded NUL, and must remain engine-root-relative. POSIX rooted paths, backslash-rooted/UNC/device forms, Windows drive-absolute forms, and Windows drive-relative forms such as `C:foo` are rejected. Lexical `..` may normalize within the root, but traversal above the root is rejected. `PackageEntry` and `VirtualPath` require a valid mount ID; `Generated` requires a valid generator ID.

Dependency and tag lists are bounded before catalog mutation. Dependency IDs and tags must be valid. Self dependency, duplicate dependency IDs, and duplicate tags are rejected. Normalization is performed on a detached candidate before insertion. Allocation failure during candidate copy/normalization or unordered-map insertion propagates as `std::bad_alloc` and leaves the pre-call catalog state unchanged.

## Dependency manifest audit

Manifest traversal is iterative, so a deep valid graph does not depend on process stack depth. Required missing dependencies return `asset.missing_dependency`; optional missing dependencies remain represented in the manifest. Cycles return `asset.dependency_cycle`.

Diamond duplicates are collapsed by `AssetId`. Requiredness is the logical OR of every reachable incoming dependency edge, independent of traversal order. The output is materialized after traversal and sorted by `AssetId::Raw()`, so observable manifest ordering does not depend on unordered-container iteration.

## Query and lifecycle audit

`FindById`, `FindByType`, `FindByTag`, and `Resolve` return detached values. Mutating returned metadata/location does not mutate the catalog. Type/tag query results are sorted by asset ID. Invalid non-Result selectors return empty/not-contained. Result-producing `Resolve` and `BuildDependencyManifest` distinguish invalid identity from valid-but-missing identity.

The only lifecycle transition is mutable catalog to sealed catalog. `Seal()` is idempotent and cannot reopen the catalog. Registration after seal returns `asset.catalog_sealed` before validation or insertion and leaves the catalog unchanged. There is no shutdown state because Assets owns no external resource or registration requiring cleanup.

## Failure atomicity and observable state

`RegisterAsset` performs all semantic validation and candidate normalization before touching `assets_`. Duplicate detection happens before insertion. The final `unordered_map::emplace` is the single authoritative publication step; allocation failure during that operation leaves the container unchanged. The fault-injection regression uses named implementation-local seams at the metadata candidate-build boundary and the final catalog-publication boundary. Each injected `std::bad_alloc` must preserve the pre-existing record and query results, and the same registration must succeed on retry. The test no longer replaces process-global `operator new`/`operator new[]` or depends on STL/CRT allocation order.

The module has no multi-container commit, external callback/backend side effect, rollback, reconciliation state, event/journal publication, or generated counter. `Seal()` mutates only one boolean and cannot fail after mutation.

## Persistence boundary

Assets exposes no snapshot/restore API and owns no runtime-generated identity/revision continuity that must be restored locally. Goal 3.5 therefore does not invent catalog persistence. A persistent catalog producer, if introduced by composition/bootstrap, must rebuild the catalog through the same validated registration contract and is outside this local module freeze.

## LOCAL_READY applicability

The applicable LOCAL_READY criteria are public contract classification, registration preconditions/success/failure, invalid identity handling, primary-state consistency, seal lifecycle/no-op behavior, single-record failure atomicity, empty/boundary/duplicate behavior, deterministic query/manifest ordering, and regression coverage.

Secondary-index consistency, generated counters/revisions, false event/revision publication, shutdown/retry cleanup, multi-container mutation, external transaction/reconciliation, callback failure, stale reusable identity, and persistence restore criteria are `N/A` with the module-specific ownership reasons above.

## Verification

Before build work, the extracted source tree was copied to a separate task backup directory and checksummed. All edits and build directories are isolated under the dedicated Goal 3.5 task directory.

Block C replaces the process-global allocator sweep with deterministic named seams. The updated suite passes GCC 14 Debug/Release and Clang 17 Debug/Release with warnings treated as errors, GCC 14 ASan+UBSan, and a GCC libstdc++ debug-iterator/assertion build. The required repository MSVC Debug/Release qualification is still pending, so `LOCAL_READY` is not re-admitted by this local audit.

All nine Assets public headers compile independently as the sole project include under GCC and Clang with warnings treated as errors. This corresponds to the repository public-header self-containment requirement.

The production top-level CMake configuration cannot complete in this Linux environment because `EngineBase/Platform` intentionally rejects non-Windows builds. No Windows/MSVC whole-project qualification is claimed by this local audit; that remains part of the normal repository qualification gate.
