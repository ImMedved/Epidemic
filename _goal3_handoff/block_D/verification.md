# Block D verification

Baseline: `Epidemic 21-09-2026-1.zip`
Plan: `Milestone_3_Final_Closure_4_Block_Plan_2026-09-21`, Block D.
Owned trees: Audio, Renderer, Support, Scene, and this handoff directory only.

## Audit inventory

Current generated IDs were recalculated from the baseline source after running the freeze generators for analysis. The handoff contains `222/222` public callable anchors, `352/352` reviewed mutator/lifecycle obligation cells, `10/10` lifecycle decisions, `79/79` stale-identity decisions and `17/17` external-boundary decisions.

Per-module inventory: `{"EngineRuntime/Audio": {"callables": 79, "external_boundary": 15, "lifecycle": 7, "obligations": 120, "stale_identity": 30}, "EngineRuntime/Renderer": {"callables": 49, "external_boundary": 1, "lifecycle": 1, "obligations": 80, "stale_identity": 18}, "EngineRuntime/Scene": {"callables": 41, "external_boundary": 0, "lifecycle": 0, "obligations": 52, "stale_identity": 11}, "EngineRuntime/Support": {"callables": 53, "external_boundary": 1, "lifecycle": 2, "obligations": 100, "stale_identity": 20}}`.

All four modules contain `15/15` reviewed dossier decisions and `37/37` LOCAL_READY decisions resolved to PASS or justified N/A in `dossier_reviews.json` and `local_ready.json`.

## Production defects and code changes

A second-pass audit reproduced one new Block D Scene defect, `M3C-SCENE-001`, before patching: large finite QuerySphere inputs could overflow both squared-distance and squared-radius arithmetic to infinity, causing a false positive intersection. `SceneRuntime::IntersectsSphere` now performs this private calculation in binary64 while retaining the public float API. `TestLargeFiniteSphereQueryDoesNotOverflowToIntersection` is the permanent regression. No public declaration changed.

`M3C-SUP-001` was manually rechecked in `RegisterRenderer()`: duplicate `RendererServices` detection is performed before dependency resolution, `EnsureMainView()`, or Scene mutation. `TestIndividualRegistrationFailuresAreAtomic` remains the permanent regression.

The handoff defect registry contains the ten owned `G3-*` defects, `M3C-SUP-001`, and the newly reproduced `M3C-SCENE-001`, each with final fix symbol, permanent regression, registered target and status `REVIEWED`.

## Executed verification

Linux/GCC 14 equivalent qualification with `-std=c++20 -Wall -Wextra -Wpedantic -Werror -O0 -g`:

* `EpidemicRuntimeAudioTests`: PASS.
* `EpidemicRuntimeRendererTests`: PASS.
* `EpidemicRuntimeSceneTests`: PASS after `M3C-SCENE-001` fix; Clang ASan+UBSan PASS; the standalone pre-patch reproducer fails on baseline and passes after the fix.
* `EpidemicRuntimeSupportTests`: PASS after relinking the fixed Scene object into the previously warnings-as-errors qualified Runtime dependency object set. Support production/test code also passed a selective Clang ASan+UBSan run while using the qualified dependency objects.
* `EpidemicRuntimeIntegrationTests`: PASS after relinking with the fixed Scene object.

Handoff structural validation performed against the repository validators in-memory:

* all 222 current callable IDs map to exact reviewed contract and registered-test anchors;
* merged `coverage_manifests.py` schema/inventory validation: PASS;
* merged module dossier review registry validation: PASS;
* a clean 78-module LOCAL_READY ledger with only Block D merged, plus Block D coverage reviews, passes `validate_ledger`: PASS.

The first Block D handoff draft was re-audited and rejected as too permissive because some assertion anchors were mechanically selected as the first assertion in a mapped test. This corrected handoff derives assertion anchors only from the exact test functions explicitly mapped to each callable in the module-local evidence tables, preferring assertions/guards adjacent to the callable/type under review. Structural validators are necessary but were not treated as sufficient semantic proof.

Official Windows/MSVC Debug/Release, ClangCL self-containment, full 94-test profile, shared canonical regeneration and final `LOCAL_READY` promotion are intentionally not performed by this parallel block. The plan assigns those shared operations to serial convergence after Blocks A-D merge.

## Files changed

Only:

* `EngineRuntime/Scene/src/scene_runtime_impl.cpp`
* `EngineRuntime/Scene/tests/scene_tests.cpp`
* `_goal3_handoff/block_D/public_api_anchors.json`
* `_goal3_handoff/block_D/coverage_reviews.json`
* `_goal3_handoff/block_D/dossier_reviews.json`
* `_goal3_handoff/block_D/local_ready.json`
* `_goal3_handoff/block_D/verification.md`

No shared `docs/freeze/*.json|md`, root plan, CMake registration, EngineBase, EngineFramework, or another Runtime module belongs in this delta.
