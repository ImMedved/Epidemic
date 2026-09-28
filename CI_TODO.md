# Local CI implementation record

AgentEnforcer2 blueprint: https://github.com/Artemonim/AgentEnforcer2

The blueprint is adapted to Epidemic rather than copied verbatim. The implemented
three-tier path is `run.ps1` -> `build.ps1` -> `tools/ci/build_cpp.ps1`.

| AgentEnforcer2 stage | State | Epidemic implementation |
|---|---|---|
| self-check | implemented | PowerShell parser, JSON config and required-tool checks |
| fmt | skip | No repository-wide clang-format policy exists yet; adopt a baseline before enforcing |
| lint | implemented | Architecture/freeze validators plus MSVC `/W4 /WX` builds |
| line-limits | skip | Establish an accepted current-tree baseline before adding warn/fail budgets |
| compile | implemented | Six selectable CMake/MSVC profiles |
| test | implemented | Exact CTest manifests, full suites, module/regex filtering, repeat-until-fail |
| coverage | skip | Always reported; select a Windows C++ collector and collect a non-blocking baseline before thresholds |
| security | skip | No dependency manifest/SAST policy; architecture and public-surface gates still run |
| build | implemented | Debug/Release builds and public-header self-containment |
| archive | skip | Local test/debug runner does not package releases |

## Follow-up policy decisions

- Choose a coverage backend (Visual Studio dynamic coverage, OpenCppCoverage, or LLVM source coverage).
  First record a baseline without a fail threshold; only then approve warn/fail policy.
- Decide whether clang-format should become authoritative. Until then CI remains read-only and does not
  rewrite the working tree.
- Establish line/file-count baselines before enabling maintainability budgets.
- Add a dependency/SAST stage if third-party manifests or package-manager lockfiles enter the project.

## Extension contract

- Add tests through normal CMake `add_test()` registration. The runner discovers them automatically.
- Update `docs/freeze/ctest_manifest.json` intentionally when the exact profile matrix changes.
- Keep tool-specific work in `tools/ci/build_cpp.ps1`; keep `run.ps1` thin.
- Any new stage must emit `ok`, `warn`, `fail`, `cached`, or `skip`, and must preserve full output in
  `.ci_cache/logs/`.
