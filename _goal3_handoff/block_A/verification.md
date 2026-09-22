# Block A verification handoff

Baseline: `Epidemic 21-09-2026-1.zip`. Scope: RuntimeFoundation, Time, Animation, Environment only. Shared `docs/freeze/*.json`, root milestone files, EngineBase, EngineFramework and other Runtime module trees were not edited.

## Production defect

`M3C-ANIM-002` is independently reproduced on the clean baseline through the normal runtime path with playback rate `0.1` and deltas `36028797018963948`, `10`, `10` microseconds. Baseline local times are `3602879701896394`, `3602879701896395`, `3602879701896396`, while the exact represented-rate accumulation and one combined `36028797018963968 us` tick require `3602879701896397`.

The fix keeps the exact `2^-1074` fraction grid as private authoritative `AnimatorRecord` state across ticks and derives the existing `double` remainder mirror from it. No public header or public callable changes. `Play()`, `Stop()`, animator initialization, zero-duration `Crossfade()` replacement and `SetFractionalMicrosecondsForTesting()` keep exact and mirrored state synchronized. The exact residual lives in the staged `AnimatorRecord`, so evaluator Result failure, evaluator exception, pose-sink Result failure and pose-sink exception do not publish it before the rest of the animator commit.

Permanent regression: `EngineRuntime/Animation/tests/animation_tests.cpp::TestStatefulPlaybackRemainderPartitionAndFailureAtomicity` in `EpidemicRuntimeAnimationTests`. It checks the production-path reproducer, combined-vs-partitioned execution, all four fallible evaluation/publication paths, exact-grid rollback, retry-once behavior and Play/Stop/zero-duration-Crossfade reset semantics.

A separate temporary public-runtime oracle checked 5,004 deterministic/random multi-tick partition cases against Python exact `Fraction` accumulation of the represented binary64 playback rate. Final local time and the bit pattern of the mirrored remainder matched in every case.

## Regression qualification

Direct platform-neutral module builds use C++20 with `-Wall -Wextra -Wpedantic -Werror`. RuntimeFoundation, Time, Animation and Environment pass GCC Debug (`-O0`), GCC Release (`-O2`), Clang Debug (`-O0`) and Clang Release (`-O2`). After the second-pass evidence audit added explicit RuntimeFoundation/Animation public-contract assertions, the changed RuntimeFoundation and Animation tests were rerun in all four compiler/optimization combinations and remain green.

Clang ASan+UBSan passes RuntimeFoundation, Time, Animation and Environment; Animation additionally passes `-fsanitize=float-cast-overflow`.

All 29 public headers in the four owned modules are byte-identical to the baseline and pass standalone GCC and Clang C++20 compilation with warnings-as-errors. `architecture_ownership.py --check`, `ctest_manifest.py --check` and `ci_gate_contract.py --check` pass.

## Evidence/admission verification

The second pass found and corrected an evidence-only defect in the first Block A handoff: several callable test anchors were syntactically valid but too generic, and the dossier handoff did not retain the evidence/rationale required by the final-closure plan. No production behavior change was required for that issue.

The corrected handoff contains:

* public callable anchors: 207/207, each with a current contract anchor and registered module-test target;
* mutator obligation decisions: 136/136, with per-callable handoff evidence/rationale for preconditions, success, failure and no-op review;
* lifecycle candidates: 7/7 reviewed;
* stale-identity candidates: 31/31 reviewed;
* external-boundary candidates: 2/2 reviewed;
* dossier fields: 15/15 for each of RuntimeFoundation, Time, Animation and Environment, with handoff evidence/rationale retained separately from the canonical status-only registry shape;
* local-ready criteria: 37/37 decisions for each of the four modules;
* defect trace records: 11, comprising 7 G3 and 4 M3C records.

A temporary serial-integration copy regenerated the current inventories, merged only the Block A handoff records, then reran the real project validators. `module_dossiers.py --check`, `coverage_manifests.py --check`, `public_api_inventory.py --check` and `public_surface_manifest.py --check` all pass. `public_api_inventory.py` reports 4311 exact public callables and zero `UNCLASSIFIED`; public surface contains 232 headers. `local_ready_contract.py --check` reports no RuntimeFoundation, Time, Animation or Environment errors. Its remaining errors are the baseline Assets stale-anchor debt owned by Block B and were deliberately not modified here.

All machine-readable anchor strings in the Block A handoff were independently checked to reference an existing file, valid current line/range and matching symbol text. All 207 public callable test anchors use one of the four registered targets `EpidemicRuntimeFoundationTests`, `EpidemicRuntimeTimeTests`, `EpidemicRuntimeAnimationTests` or `EpidemicRuntimeEnvironmentTests`.

MSVC Debug/Release and ClangCL cannot be executed in this Linux environment and remain mandatory serial qualification gates. The handoff therefore contains the completed candidate evidence, but the serial integrator must not publish canonical final `LOCAL_READY` until the official Windows matrix passes.
