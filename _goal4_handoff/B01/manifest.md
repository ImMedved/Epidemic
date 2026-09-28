# B01 handoff manifest

Baseline supplied by the user: `Epidemic Engine 27-09-26-1.zip` on 2026-09-27. Block: `b01_core_runtime_boundary`.

Owned production modules: Foundation, SupportRandom, Queries, Facts, Time, RuntimeBridge. Exclusive documentation ownership used: `EngineFramework/README.md`, `docs/EngineFramework/README.md`, and the six B01 module documents.

Implemented changes:

1. `G4-RB-001`: RuntimeBridge visibility geometry promotes finite Runtime coordinates before subtraction, computes the norm with wide `double`/`std::hypot`, rejects a true distance that cannot be represented by Runtime `RaycastQuery::max_distance`, normalizes in wide arithmetic, and narrows only validated finite values. The regression covers normal 3-4-5 geometry, zero/near-zero, `FLT_MAX`, opposite-sign `±FLT_MAX`, and diagonal out-of-range distance.
2. `G4-DOC-001`: corrected the Framework README documentation root, added `docs/EngineFramework/README.md`, and added B01 contract/audit docs for Foundation, SupportRandom, Queries, Facts, Time and RuntimeBridge. The root index lists B02-B08 document paths without creating broken links in an isolated B01 delta.
3. `G4-INFRA-001` B01 portion: removed process-global allocator-fault injection from `foundation_tests.cpp`, `queries_tests.cpp`, `facts_tests.cpp`, and `time_tests.cpp`. Replaced it with deterministic module-local fail points at named publication boundaries. The shared helper and shared sweep headers remain read-only and unchanged.
4. Added required B01 handoff artifacts: manifest, defects, dossier reviews, coverage reviews, public API anchors and cross-block findings. Added `local_ready.json` as the explicit block-local 37-criteria projection for all six owned modules.
5. `G4-DOC-001` has an executable regression in the existing Foundation target: `TestFrameworkDocumentationRoot()`.

B01 allocation-fault publication points:

- Foundation: `type_registry.publish`, `tag_registry.publish`.
- Queries: `provider_registration.publish`, `freeze.publish`.
- Facts: `transaction_commit.publish`, `direct_publish.publish`, `batch_merge.publish`.
- Time: `clock_registration.publish`, `schedule.record_publish`, `schedule.index_publish`, `reschedule.publish`, `collect_due.publish`, `clock_sync.publish`.

Validation executed in this worker environment:

- Repository CMake `full-debug` configure was attempted and stopped at the repository's intentional Windows-only `EngineBase/Platform` gate before generation.
- GCC 14.2, C++23, `-Wall -Wextra -Werror`, function/data sections plus linker GC: Foundation, SupportRandom, Queries, Facts and Time production translation units compile; their five existing module test executables link and pass.
- The Foundation test emits its expected negative pre-state comparator diagnostic (`foundation.test.revision_only ... covers=0`) while returning success.
- RuntimeBridge source and `runtime_bridge_tests.cpp` compile with the same warnings-as-errors settings; the linked test executable passes.
- A regenerated public API inventory retains the admission B01 callable counts exactly: Foundation 104, SupportRandom 28, Queries 43, Facts 49, Time 31, RuntimeBridge 71. The internal fail-point symbols are not inventoried as public callables.
- Canonical `public_api_inventory.validate_anchors` passes after merging all 326 B01 API anchors into the repository anchor registry: 0 errors.
- Canonical `module_dossiers.validate_reviews` passes after merging the six B01 dossier reviews: 0 errors.
- Canonical `local_ready_contract.validate_ledger` passes after merging the B01 37-criteria projection and coverage decisions into the repository ledgers: 0 errors. The B01 coverage review contains 326 reviewed APIs, 344 reviewed mutation obligations, 5 reviewed lifecycle candidates, 66 reviewed stale-identity candidates and 4 reviewed external-boundary candidates.
- B01-owned test `.cpp` files containing `allocation_fault_injection.h`: 0. Repository-wide remaining users belong to other blocks.
- Official Windows 11/MSVC Debug/Release execution was not available in this worker environment and is therefore not claimed here. Serial integration should run the prescribed MSVC matrix after merge.

No shared CMake, `docs/freeze/**`, `.github/**`, `cmake/**`, shared allocation/sweep helper, work-plan, or other Bxx production directory is changed by this delta.

Changed/new files in this delta:

- `EngineFramework/README.md`
- `docs/EngineFramework/README.md`
- `docs/EngineFramework/modules/foundation.md`
- `docs/EngineFramework/modules/support_random.md`
- `docs/EngineFramework/modules/queries.md`
- `docs/EngineFramework/modules/facts.md`
- `docs/EngineFramework/modules/time.md`
- `docs/EngineFramework/modules/runtime_bridge.md`
- `EngineFramework/BaseInfrastructure/Foundation/include/epidemic/gameplay/foundation/type_registry.h`
- `EngineFramework/BaseInfrastructure/Foundation/src/gameplay_foundation.cpp`
- `EngineFramework/BaseInfrastructure/Queries/include/epidemic/gameplay/queries/gameplay_queries.h`
- `EngineFramework/BaseInfrastructure/Queries/src/gameplay_queries.cpp`
- `EngineFramework/BaseInfrastructure/Facts/include/epidemic/gameplay/facts/gameplay_facts.h`
- `EngineFramework/BaseInfrastructure/Facts/src/gameplay_facts.cpp`
- `EngineFramework/BaseInfrastructure/Time/src/gameplay_time.cpp`
- `EngineFramework/RuntimeBoundary/RuntimeBridge/src/runtime_bridge.cpp`
- `EngineFramework/DevelopmentInfrastructure/Tests/foundation_tests.cpp`
- `EngineFramework/DevelopmentInfrastructure/Tests/queries_tests.cpp`
- `EngineFramework/DevelopmentInfrastructure/Tests/facts_tests.cpp`
- `EngineFramework/DevelopmentInfrastructure/Tests/time_tests.cpp`
- `EngineFramework/DevelopmentInfrastructure/Tests/runtime_bridge_tests.cpp`
- `_goal4_handoff/B01/manifest.md`
- `_goal4_handoff/B01/defects.json`
- `_goal4_handoff/B01/dossier_reviews.json`
- `_goal4_handoff/B01/coverage_reviews.json`
- `_goal4_handoff/B01/public_api_anchors.json`
- `_goal4_handoff/B01/local_ready.json`
- `_goal4_handoff/B01/cross_block_findings.md`

## 2026-09-28 closure-grade evidence pass

- Re-reviewed all 326 B01 callable records. Every callable now points to a callable-specific contract row and a concrete invocation/assertion anchor; no test record uses `main()` as proof.
- Rebuilt all six 37-criterion LOCAL_READY projections as typed evidence objects containing every required evidence kind. No fallback evidence or empty N/A rationale remains.
- Normalized all three B01 defect records with `logical_id`, registered `targets`, an exact legacy `regression` anchor and exact per-target `regressions` mappings. `G4-DOC-001` points to the real `TestFrameworkDocumentationRoot` regression.
- Official Windows/MSVC 2026 `/W4 /WX` qualification from `build/framework-warnings`: 6/6 B01 owned targets built and passed in both Debug and Release as part of the 20-target block-A matrix.
- No public header, production source or test source was changed by this closure evidence pass.
