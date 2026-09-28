# B05 verification

Fresh baseline application was used for verification. Only B05-owned source/test files and B05 handoff/docs were overlaid.

Executed local gates:

1. GCC Debug strict build and 5/5 CTest pass.
2. GCC Release strict build and 5/5 CTest pass.
3. Clang 17 Debug strict build and 5/5 CTest pass.
4. Clang 17 AddressSanitizer + UndefinedBehaviorSanitizer with leak detection, 5/5 pass.
5. Disposable canonical evidence regeneration confirms 316 exact B05 callables and no callable-count delta.
6. Coverage counts: 308 mutation obligations, 9 lifecycle, 74 stale-identity, 1 external-boundary.
7. Shared process-global allocator/restore helper reference audit in owned tests: zero.
8. Admission defects G4-PER-001/002/003 and G4-SIM-001/002 have concrete regression paths and targets in `defects.json`.

The Linux harness compiles the real module libraries plus their existing module-specific tests. It does not replace the serial Windows/MSVC full-tree qualification required after merge.

Repetition/packaging preflight: Release suite repeated 10 times, 50/50 passes. Normalized `git diff --check` is clean. Ownership path audit is clean.

Independent post-package re-audit:

9. Re-applied delta to a newly extracted baseline; GCC Debug, GCC Release, Clang Debug and Clang ASan+UBSan all pass 5/5.
10. GCC `_GLIBCXX_DEBUG` pass: 5/5, checking container/iterator use after the Simulation deque-to-list change.
11. Isolated public-header consumers: Perception, Knowledge, NavigationSemantics, AI and Simulation compile standalone under GCC and Clang with `-Wall -Wextra -Wpedantic -Werror` (10/10).
12. `G4-PER-003` regression was strengthened to cover an above-`sqrt(INT64_MAX)` distance that is still inside range (`3,050,000,000 < 3,100,000,000`), proving the linear distance no longer collapses to the old saturated-square plateau.
13. Whole-engine public callable generator produces 4311 callables for both pristine baseline and patched tree; no callable surface delta was introduced by B05.
14. Root CMake configure is intentionally unavailable on this Linux host because the Platform slice rejects non-Windows hosts; Windows/MSVC and remote CI therefore remain serial integration gates.

Second independent re-audit correction:

15. The original `G4-PER-003` above-square-root assertion was made direct: `3,050,000,000 mm` is above `floor(sqrt(INT64_MAX))` and still inside the `3,100,000,000 mm` range, so the corrected distance path itself is exercised.
16. A remaining `G4-SIM-001` boundary was discovered during re-audit: the last external commit could consume the final available task revision/journal slot and leave no capacity for interval-summary finalization. `ContinueIntervalExecution` now stages terminal task plus interval-summary publication before the external commit.
17. New Simulation regressions cover revision `MAX-1`, one remaining journal sequence, terminal summary allocation failure before external commit, unchanged snapshot on each failure, and successful retry after the allocation fault is cleared.
18. After both re-audit corrections, the complete B05 suite was rebuilt and rerun under GCC Debug, GCC Release, Clang Debug, Clang ASan+UBSan with leak detection, and GCC `_GLIBCXX_DEBUG`: 25/25 profile test executions passed. The corrected Release suite was additionally repeated 10 times: 50/50 executions passed. Isolated public-header consumers remain 10/10 under GCC/Clang strict warnings.

Closure qualification on 2026-09-28:

19. Windows/MSVC Debug `/W4 /WX`: 5/5 owned targets built; CTest 5/5 PASS.
20. Windows/MSVC Release `/W4 /WX`: 5/5 owned targets built; CTest 5/5 PASS.
21. Exact-anchor audit: 316/316 callable contract/test records resolve, generic test = 0, generic contract = 0.
22. Raw criterion audit: 185/185 criteria complete, source gaps = 0, invalid/generic N/A = 0.
23. Defect audit: 6/6 physical records have a logical ID, registered targets and exact per-target line anchors.
