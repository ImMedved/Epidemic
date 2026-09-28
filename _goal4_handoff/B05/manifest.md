# Goal 4 B05 delta manifest

Baseline input: `Epidemic Engine 27-09-26-1(1).zip`, SHA-256 `3ef72cfa1d28135611b89f3357aed726bd081b25cc90e949d51e5b91e3074897`.

Block: `B05 Perception, Knowledge, AI and Simulation`.

Owned modules: `Perception`, `Knowledge`, `NavigationSemantics`, `AI`, `Simulation`.

Block-local result: five owned modules have complete B05 review artifacts with 316/316 API rows, 308/308 mutation obligations, 9/9 lifecycle candidates, 74/74 stale-identity candidates, 1/1 external-boundary candidate, 15/15 dossier fields per module and 37/37 `PASS` decisions per module in the block-local projection.

Confirmed fixes:

* `G4-PER-001`: exact signed coordinate deltas are formed before floating conversion; regression `TestSpatialBoundaryMath`.
* `G4-PER-002`: awareness interval/timestamp advancement uses bounded integer arithmetic; regression `TestDecayBoundaryArithmetic`.
* `G4-PER-003`: large distance/range decisions no longer narrow squared distance through signed 64-bit storage; regression `TestSpatialBoundaryMath`.
* `G4-SIM-001`: local terminal revision/journal publication is staged before accepted external executor Commit.
* `G4-SIM-002`: interval, summary and journal publication are staged before no-fail authoritative commit with module-local fault regressions.
* `G4-INFRA-001` B05 share: owned process-global allocation/restore helper uses were replaced by private module seams; shared helpers remain untouched.

Validation executed:

* GCC C++23 Debug, `-Wall -Wextra -Wpedantic -Werror`: all five B05 module test executables passed.
* GCC C++23 Release, same strict warnings: all five passed.
* Clang 17 C++23 Debug, same strict warnings: all five passed.
* Clang 17 ASan+UBSan Debug with leak detection: all five passed.
* Regenerated evidence in a disposable copy: block remains exactly 316 API, 308 obligations, 9 lifecycle, 74 stale, 1 external.
* Owned test grep: zero references to shared `allocation_fault_injection.h`, `restore_fault_sweep.h`, `mutation_fault_sweep.h`, or global new/delete hooks.

Public-surface review: `SimulationService` private storage in the public header changes `std::deque` to `std::list` for completed summaries and the change journal to support no-throw splice publication. No public callable changes; block callable count remains 316. This header/private-layout hash delta must be accepted when the serial integrator regenerates canonical public-surface evidence. Stable C++ binary ABI is not part of the freeze promise.

Environment limitation: canonical Windows/MSVC Base/Runtime/Full profiles, exact canonical CTest manifest and remote Architecture Freeze/ClangCL CI cannot be claimed from this Linux container and remain serial integration gates.

Remaining blockers: no known B05 ownership-local code blocker. Canonical post-merge Windows/CI convergence remains.

Shared/read-only files modified: none. No `docs/freeze/**`, common work plan, shared CMake, `.github/**`, `cmake/**` or shared allocation/sweep helper is included in the delta.

Additional recheck:

* Release B05 suite repeated 10 times after source stabilization: 50/50 module test executions passed.
* Normalized temporary-Git `git diff --check` with Windows-style `core.autocrlf=true`: clean.
* Ownership path audit: zero files outside B05 production/tests/docs/handoff.

Changed/new files (29):

* `EngineFramework/DevelopmentInfrastructure/Tests/ai_tests.cpp`
* `EngineFramework/DevelopmentInfrastructure/Tests/knowledge_memory_tests.cpp`
* `EngineFramework/DevelopmentInfrastructure/Tests/navigation_semantics_tests.cpp`
* `EngineFramework/DevelopmentInfrastructure/Tests/perception_tests.cpp`
* `EngineFramework/DevelopmentInfrastructure/Tests/simulation_tests.cpp`
* `EngineFramework/GameplayWorldStateOwners/AI/src/ai.cpp`
* `EngineFramework/GameplayWorldStateOwners/AI/src/ai_test_seam.h`
* `EngineFramework/GameplayWorldStateOwners/Knowledge/src/knowledge.cpp`
* `EngineFramework/GameplayWorldStateOwners/Knowledge/src/knowledge_test_seam.h`
* `EngineFramework/GameplayWorldStateOwners/NavigationSemantics/src/navigation_semantics.cpp`
* `EngineFramework/GameplayWorldStateOwners/NavigationSemantics/src/navigation_semantics_test_seam.h`
* `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp`
* `EngineFramework/GameplayWorldStateOwners/Perception/src/perception_test_seam.h`
* `EngineFramework/GameplayWorldStateOwners/Simulation/include/Epidemic/GameFramework/Simulation/simulation.h`
* `EngineFramework/GameplayWorldStateOwners/Simulation/src/simulation.cpp`
* `EngineFramework/GameplayWorldStateOwners/Simulation/src/simulation_test_seam.h`
* `_goal4_handoff/B05/coverage_reviews.json`
* `_goal4_handoff/B05/cross_block_findings.md`
* `_goal4_handoff/B05/defects.json`
* `_goal4_handoff/B05/dossier_reviews.json`
* `_goal4_handoff/B05/local_ready.json`
* `_goal4_handoff/B05/manifest.md`
* `_goal4_handoff/B05/public_api_anchors.json`
* `_goal4_handoff/B05/verification.md`
* `docs/EngineFramework/AI.md`
* `docs/EngineFramework/Knowledge.md`
* `docs/EngineFramework/NavigationSemantics.md`
* `docs/EngineFramework/Perception.md`
* `docs/EngineFramework/Simulation.md`

Independent re-audit after packaging:

* Re-applied the ZIP to a newly extracted baseline and repeated GCC Debug/Release, Clang Debug and Clang ASan+UBSan: 5/5 B05 tests passed in every profile.
* Added `_GLIBCXX_DEBUG` container/iterator validation: 5/5 passed.
* Compiled all five B05 public headers as isolated consumers with GCC and Clang under strict warnings: 10/10 header checks passed.
* Strengthened `G4-PER-003` regression so a distance of 3,050,000,000 mm, above `floor(sqrt(INT64_MAX))` but inside a 3,100,000,000 mm range, must remain audible with strictly lower attenuation than the boundary value. This directly exercises the formerly saturated linear-distance path rather than only the exact out-of-range precheck.
* Public API inventory regeneration reports the same whole-engine callable total on pristine baseline and patched tree (`4311`); the B05 block remains 316 callables.
* Root-project CMake cannot be configured in this Linux environment because `EngineBase/Platform` intentionally hard-fails on non-Windows hosts. This is an environment gate, not a B05 build failure; canonical MSVC/full-tree validation remains for serial integration.

Second independent re-audit correction:

* Strengthened `G4-PER-003` so the above-`sqrt(INT64_MAX)` regression remains inside the configured audible range and directly traverses the corrected linear-distance path.
* Found and fixed an additional `G4-SIM-001` terminal-capacity boundary in the first B05 delta: when the last unfinished layer had only one revision or one journal sequence left, external work could be accepted even though final interval-summary publication required a second terminal record. The final-layer path now stages both terminal revisions, both journal records and the summary node before external `Commit`.
* Added regressions for restored revision `MAX-1`, final journal-sequence exhaustion, and terminal summary-allocation failure. All verify zero external commits and complete snapshot equality on preflight failure; the allocation regression additionally verifies successful retry.

## 2026-09-28 closure evidence refresh

* Replaced all 244 generic callable test anchors with exact registered-target call/assertion anchors while preserving the existing callable-specific doc rows.
* Rebuilt all 185 LOCAL_READY criteria as complete typed evidence; no merge fallback is required.
* Converted all six defect records to machine-readable logical IDs and exact per-target regressions. `G4-SIM-001` records all four terminal-capacity boundaries and `G4-SIM-002` records both interval and summary publication regressions.
* Windows/MSVC `/W4 /WX` Debug and Release builds passed for all five B05 targets; CTest passed 5/5 in each configuration.
* The strict raw-handoff quality audit reports zero generic API test/contract anchors, zero criterion source gaps, zero generic/empty N/A rationales and zero stale anchors for B05.
