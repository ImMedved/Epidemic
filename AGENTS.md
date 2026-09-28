# Epidemic agent execution contract

## Doctrine

- Preserve the dependency direction `EngineBase -> EngineRuntime -> EngineFramework`; lower layers never depend on higher layers.
- Do not edit generated canonical freeze JSON manually. Update its source inventory/handoff and run the owning generator.
- A change is not complete until the relevant local CI entry point passes.

## Project map

- `EngineBase/`: platform, diagnostics, input, memory, RHI and application lifecycle.
- `EngineRuntime/`: independent runtime majors; cross-major technical composition belongs in Runtime Support.
- `EngineFramework/`: reusable gameplay state owners, integration adapters and Framework tests.
- `docs/freeze/`: architecture, public-surface, coverage and LOCAL_READY validators.
- `_goal4_handoff/`: raw Goal 4 evidence; canonical outputs are produced by its merge script.
- `tools/ci/`: language/toolchain layer for the local CI runner.

## Read-only/generated zones

- Treat `build/`, `.ci_cache/`, `.enforcer/`, `.idea/` and `Deltas/` as local artifacts.
- Do not hand-edit `docs/freeze/coverage_manifests.json`, `public_api_anchors.json`,
  `module_dossier_reviews.json` or `local_ready_ledger.json`.

## Quality control

- Default final verification: `./run.ps1 -Mode Full -NoCache`.
- For dependency/profile changes: `./run.ps1 -Mode Matrix -NoCache`.
- For a focused iteration: `./run.ps1 -Mode Module -Module <name>`.
- Give full local CI at least a 25-minute terminal timeout; command-level heartbeats and logs are built in.

The runner is adapted from the AgentEnforcer2 blueprint:
https://github.com/Artemonim/AgentEnforcer2
