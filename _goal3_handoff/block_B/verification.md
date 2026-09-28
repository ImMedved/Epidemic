# Goal 3 final closure: Block B verification, rechecked

Baseline: `Epidemic 21-09-2026-1.zip`.
Plan: `Milestone_3_Final_Closure_4_Block_Plan_2026-09-21`, revision 2.
Owned modules: Serialization, Persistence, Assets, Resources, World.

## Scope and result

The recheck stayed strictly inside the Block B ownership boundary. The delta changes only the five owned module test files and `_goal3_handoff/block_B/**`. No production source, public header, module/root CMake file, shared canonical freeze file, EngineBase, EngineFramework, or another Runtime module is changed.

No new Block B production defect was reproduced. Production behavior therefore remains unchanged.

The first Block B handoff was structurally valid but the second-pass semantic audit found an evidence defect: some public callable test anchors pointed to a relevant test name or to the invocation of that test from `main()` without the cited assertion directly exercising the callable. This rechecked delta corrects that evidence gap. One focused `TestPublicApiEvidenceCoverage` case was added to each existing owned module test executable. The cases directly exercise or compile-time-prove the previously indirect trivial/query/helper/equality/lifetime contracts. No new CTest executable or CMake registration is introduced.

## Handoff completeness

The rechecked handoff contains:

- 234/234 current Block B public callable anchor records, each with reviewed contract and registered-test evidence;
- 288/288 mutator obligations reviewed;
- 7/7 lifecycle candidates reviewed;
- 56/56 stale-identity candidates reviewed;
- 1/1 external-boundary candidate reviewed;
- 8/8 owned G3 production defect records with final fix/regression/target traceability;
- 75/75 dossier field decisions, 15 for each owned module;
- 185/185 LOCAL_READY criterion decisions, 37 for each owned module.

All 1070 exact `path:line::snippet` anchors/assertions in the handoff resolve against the final Block B tree. A semantic anchor audit also verified that no public callable test anchor points to a `main()` dispatch line and that every ordinary callable name is present in the body of the anchored test. Operators, constructors and virtual destructors are anchored to direct expressions or explicit compile-time assertions.

`local_ready.json` intentionally keeps `LOCAL_READY_CANDIDATE`. Final canonical `LOCAL_READY` promotion belongs to serial convergence after the required Windows/MSVC qualification.

## Added evidence regressions

The existing registered executables now contain one additional focused evidence case each:

- `EpidemicRuntimeSerializationTests::TestPublicApiEvidenceCoverage` covers virtual interface lifetime, default `SerializedDocument`, `SchemaVersion` ordering, `MigrationKey` equality/hash, `ISerializer::GetCppType`, and `CreateSerializationError` directly.
- `EpidemicRuntimePersistenceTests::TestPublicApiEvidenceCoverage` covers virtual interface lifetime, protection-mask conversion/equality, `PersistenceLocation` equality, `FindByLocation`, `ListTombstones`, `IsDirty`, and `RemoveZoneOverride` through a committed store transaction.
- `EpidemicRuntimeAssetsTests::TestPublicApiEvidenceCoverage` covers virtual interface lifetime, `AssetLocation` construction/canonicalization/validation, enum validation, and value equality contracts.
- `EpidemicRuntimeResourcesTests::TestPublicApiEvidenceCoverage` covers virtual interface lifetime, byte payload access, resource value equality, and direct `GetResourceId` mapping after lease acquisition.
- `EpidemicRuntimeWorldTests::TestPublicApiEvidenceCoverage` covers virtual interface lifetime, enum validators, placement region/chunk helpers, public value equality operators, and direct `ValidateWorldObjectInvariant` success.

These tests use only module-local/public contracts and do not introduce Runtime peer dependencies.

## Existing defect regression locks requalified

Assets: `G3-ASSET-001..003` remain covered by `TestDependencyManifestMergesRequirednessIndependentlyOfTraversalOrder`, `TestPathValidationRejectsTraversalAndRequiresMounts`, and `TestInvalidQueryInputsAreControlled`.

Resources: `G3-RES-001..004` remain covered by `TestDependencyReplacementStrongCommit`, `TestWaitingOwnerPublicationFailureKeepsLoadOwner`, `TestLocalFinalizationFailureDoesNotReloadArtifact`, and `TestSelectedJobOwnershipSurvivesLoadingPublicationFailure`.

Persistence: `G3-PERS-001` remains covered by `TestInMemoryBackendStrongCommitUnderAllocationFaults` and `TestCandidateAllocationFailureLeavesBackendAndLiveStateOld`.

Serialization closure fault/atomicity coverage remains in `TestArchiveAllocationFailureAtomicity`, `TestDeserializeAllocationFailureAtomicity`, `TestApplyMigrationsAllocationFailureAtomicity`, `TestSerializeToDocumentAllocationFailureAtomicity`, and `TestFinalizeValidatesMetadataAndPreservesWriterOnFailure`.

World still has no reproduced Goal 3 production defect in this snapshot, so no World production code was changed.

## Build, regression and fault-sensitive qualification

The project root intentionally rejects this Linux host in `EngineBase/Platform`, so owned Runtime suites were built directly with RuntimeFoundation and Foundation headers. C++20 and warnings-as-errors were enabled.

Passed on the final rechecked tree:

- GCC 14.2 Debug: all five owned suites;
- GCC 14.2 Release: all five owned suites;
- Clang 17 Debug: all five owned suites;
- Clang 17 Release: all five owned suites;
- GCC Debug with `_GLIBCXX_DEBUG` and `_GLIBCXX_ASSERTIONS`: all five owned suites;
- Clang ASan + UBSan with leak detection and halt-on-UB: all five owned suites;
- 100 consecutive GCC Debug executions for each of the five suites: 500/500 passes.

All original defect regression locks execute as part of those full module suites. The added evidence cases therefore did not replace or bypass the existing fault/atomicity regressions.

Static scans over the five owned trees found no process-global test `operator new/new[]`, no `_ITERATOR_DEBUG_LEVEL` workaround, and no `TODO/FIXME/HACK` marker. No production source, public header or CMake diff exists.

The three Block B public-header differences against the stale committed manifest remain hash-only changes already present in the baseline: `Serialization/serialization_services.h`, `Serialization/serializer.h`, and `World/chunk.h`. This delta does not modify them. Temporary regeneration reports the same public callable/header structure.

## Canonical merge simulation

A fresh copy of the final Block B tree was regenerated and the Block B handoff was temporarily merged into the canonical evidence files by ID/module name, without putting those shared files into this delta.

Passed:

```text
public_api_inventory.py --check: PASS, 4311 exact public callables, zero UNCLASSIFIED
coverage_manifests.py --check: PASS
module_dossiers.py --check: PASS, 78/78 modules, 232/232 public headers, 15/15 fields
public_surface_manifest.py --check: PASS
architecture_ownership.py --check: PASS, 78/78 responsibilities/ownership domains
ctest_manifest.py --check: PASS
ci_gate_contract.py --check: PASS
```

`local_ready_contract.py --check` reports exactly 19 errors, all under `EngineRuntime/RuntimeFoundation`, which belongs to Block A. It reports zero errors for Serialization, Persistence, Assets, Resources, or World after the rechecked Block B handoff is merged.

## Responsibility and packaging checks

The module CMake boundaries were reviewed again. Serialization, Persistence, Assets, Resources and World still link only the allowed Foundation/RuntimeFoundation dependencies and retain their existing forbidden-major guards. `architecture_ownership.py --check` remains green.

The source files already use repository-tracked mixed line-ending policy: Assets tests are LF, while Persistence/Serialization/Resources/World tests are tracked as CRLF. The delta preserves those original line endings. In this Linux checkout `git -c core.whitespace=cr-at-eol diff --check` is clean; no root `.gitattributes` or repository policy file was changed because those paths are outside Block B ownership.

## Not provable here

The official Windows/MSVC Debug/Release profiles and ClangCL public-header self-containment still require the serial integrator/CI environment. The rechecked handoff therefore remains a `LOCAL_READY_CANDIDATE`, not a final canonical admission.
