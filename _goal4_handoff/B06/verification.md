# B06 verification

The supplied baseline plus this B06 delta was compiled and executed with C++23 and warnings as errors. GCC 14.2.0 Debug-like and Release-like passed 5/5 owned module tests each. Clang 17 Debug-like and Release-like passed 5/5 each, for 20/20 compiler/configuration module runs.

Clang 17 AddressSanitizer + UndefinedBehaviorSanitizer (`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, leak detection enabled) passed all 5/5 owned module tests. This is extra local evidence and does not replace Goal 6 whole-engine sanitizer qualification.

The three B06 baseline tests that used process-global allocation fault injection (`abilities_tests.cpp`, `progression_tests.cpp`, `traversal_tests.cpp`) now contain no references to `allocation_fault_injection.h`, `mutation_fault_sweep.h`, `restore_fault_sweep.h`, `FailAfter` or `allocation_fault::`. Combat and Construction had no such baseline dependency.

B06 public headers are byte-identical to the supplied baseline. Production changes are confined to Combat, Abilities, Progression and Traversal. Construction required no production change.

The Linux worker cannot run the canonical Windows-only top-level Platform configuration, so official Windows 11/MSVC Debug/Release CTest, exact manifest, public-header self-containment under MSVC and remote Architecture Freeze CI remain serial integration gates.

Independent re-verification strengthened G4-INFRA-001 evidence: every named Abilities/Progression/Traversal failure seam is exercised from a fresh fixture, failure paths compare the complete captured snapshot, and journal recovery catches `std::bad_alloc` specifically rather than using a catch-all success path.

Closure qualification on 2026-09-28:

* Windows/MSVC Debug `/W4 /WX`: 5/5 owned targets built; CTest 5/5 PASS.
* Windows/MSVC Release `/W4 /WX`: 5/5 owned targets built; CTest 5/5 PASS.
* Exact-anchor audit: 362/362 callable contract/test records resolve, generic test = 0, generic contract = 0.
* Raw criterion audit: 185/185 criteria complete, source gaps = 0, invalid/generic N/A = 0.
* Defect audit: 8/8 physical records have a logical ID, registered targets and exact per-target line anchors; `G4-B06-PROG-004` remains explicitly tracked.
