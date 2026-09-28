# B06 Goal 4 delta manifest

Baseline used by this worker: `Epidemic Engine 27-09-26-1(2).zip` supplied in the task. This delta is computed against that supplied archive.

Owned modules: `Combat`, `Abilities`, `Progression`, `Construction`, `Traversal`. Admission workload reviewed: 362 public callables, 492 mutation obligations, 11 lifecycle candidates, 115 stale-identity candidates, 0 external-boundary candidates.

## Production changes

* `G4-COMBAT-001`: portable exact signed ratio scaling replaces `long double` preserve-ratio arithmetic near int64 boundaries.
* `G4-PROG-001`: profile removal preflights revision before destructive erase.
* `G4-PROG-002`: revision-bearing profile/perk/unlock/progress paths preflight before mutation; progress commit stages track plus milestone rewards and consumes the reservation only after all required revisions can publish.
* `G4-PROG-003`: modifier generator and vector publication are staged so revision/allocation failure cannot consume an ID.
* `G4-TRAV-001`: capability/profile/mode/route/session/carrier mutation families reject revision exhaustion before authoritative state changes.
* `G4-TRAV-002`: fallible Traversal grant/profile/route/session/carrier/journal publication is staged behind module-local seams with cross-link preservation.
* `G4-B06-PROG-004`: Progression late `std::bad_alloc` during journal publication is converted to epoch rotation after accepted state rather than an escaping post-commit exception; unrelated exceptions are not swallowed.
* `G4-INFRA-001` B06 slice: all three B06 baseline users of process-global allocator fault injection (`Abilities`, `Progression`, `Traversal` tests) now use private deterministic seams. Every named failure seam is exercised from a fresh fixture with full pre/post snapshot comparison. No new catch-all success path remains. Shared helpers are unchanged.
* `Construction`: full local review found no production change required on the supplied baseline.

## Evidence and documentation

Five module docs were added under `docs/EngineFramework/`. `_goal4_handoff/B06/` records exact API/obligation/candidate review decisions, 15/15 dossier reviews, 37-criteria block-local projections, defect registry, verification and cross-block findings. Canonical generated `docs/freeze/**` files are unchanged.

## Tests executed

* GCC 14.2.0 C++23, `-Wall -Wextra -Wpedantic -Werror`: 5/5 Debug-like and 5/5 Release-like PASS.
* Clang 17 C++23 with the same warnings-as-errors policy: 5/5 Debug-like and 5/5 Release-like PASS.
* Total portable compiler/configuration module runs: 20/20 PASS.
* Clang 17 AddressSanitizer + UndefinedBehaviorSanitizer with leak detection: 5/5 PASS.
* `public_api_inventory.py`, `coverage_manifests.py`, `local_ready_contract.py`, `public_surface_manifest.py`, `module_dossiers.py`: `--check` and `--self-test` PASS on the patched tree.
* B06 public headers are byte-identical to the supplied baseline.
* Owned test scan: zero references to process-global allocation/sweep helpers; every named private failure seam has a regression invocation.
* Top-level CMake configure in this Linux worker stops at the intentional Windows-only `EngineBase/Platform` gate, so official Windows/MSVC profiles remain serial integration work.

## Remaining blockers

No known B06 code defect remains after this worker audit. Serial integration must run canonical Windows 11/MSVC Debug/Release CTest, exact manifests, public-header self-containment, architecture/freeze validators and remote Architecture Freeze CI after merging all eight deltas. Shared allocator/sweep helpers may be removed only after every Bxx slice reports zero users.

## Changed files

* `_goal4_handoff/B06/cross_block_findings.md`
* `_goal4_handoff/B06/coverage_reviews.json`
* `_goal4_handoff/B06/manifest.md`
* `_goal4_handoff/B06/dossier_reviews.json`
* `_goal4_handoff/B06/public_api_anchors.json`
* `_goal4_handoff/B06/local_ready_reviews.json`
* `_goal4_handoff/B06/defects.json`
* `_goal4_handoff/B06/verification.md`
* `EngineFramework/DevelopmentInfrastructure/Tests/abilities_tests.cpp`
* `EngineFramework/DevelopmentInfrastructure/Tests/combat_tests.cpp`
* `EngineFramework/DevelopmentInfrastructure/Tests/progression_tests.cpp`
* `EngineFramework/DevelopmentInfrastructure/Tests/traversal_tests.cpp`
* `EngineFramework/GameplayWorldStateOwners/Abilities/src/abilities.cpp`
* `EngineFramework/GameplayWorldStateOwners/Abilities/src/abilities_test_seam.h`
* `EngineFramework/GameplayWorldStateOwners/Combat/src/combat.cpp`
* `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp`
* `EngineFramework/GameplayWorldStateOwners/Progression/src/progression_test_seam.h`
* `EngineFramework/GameplayWorldStateOwners/Traversal/src/traversal.cpp`
* `EngineFramework/GameplayWorldStateOwners/Traversal/src/traversal_test_seam.h`
* `docs/EngineFramework/Abilities.md`
* `docs/EngineFramework/Combat.md`
* `docs/EngineFramework/Construction.md`
* `docs/EngineFramework/Progression.md`
* `docs/EngineFramework/Traversal.md`

## 2026-09-28 closure evidence refresh

All 362 callable records now have callable-specific contract rows plus exact registered-test call/assertion anchors; the previous 362 generic test anchors and 123 generic contract anchors are gone. All 185 raw LOCAL_READY criteria now contain complete typed evidence. The eight B06 defect records use explicit logical IDs and exact per-target regressions, including the tracked post-admission `G4-B06-PROG-004` assertion for accepted mutation plus journal-allocation failure.

Official Windows/MSVC qualification from `build/presets/full-debug` built all five B06 targets in Debug and Release with the repository warnings-as-errors policy. CTest passed 5/5 in both configurations. The strict raw-handoff evidence audit reports zero generic API tests, zero generic contracts, zero criterion source gaps, zero generic/empty N/A rationales and zero stale anchors for B06.
