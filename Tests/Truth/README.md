# Audit truth tests

This suite treats dev-log/Epidemic_EngineFramework_Audit_06-09-2026.md as a behavioral specification.
It does not reuse production test helpers or existing test executables, and it only calls public engine APIs.

## Suites

| Suite | Purpose |
| --- | --- |
| EpidemicFrameworkPublicApiTruthTests | Compiles every audited major public header together and verifies owner-domain separation. |
| EpidemicFrameworkContractTruthTests | Tests determinism, gameplay-time semantics, collision-safe identity, atomic restore, invisible reservations, world-item identity, and offer settlement provenance. |
| EpidemicFrameworkIntegrationTruthTests | Exercises the Perception -> Knowledge -> AI -> intent execution path with causal context. |
| EpidemicEngineLoadTruthTests | Exercises 100,000 entities, 20,000 schedules, 5,000 perceivers, 10,000 AI agents, and 1,000 complete Runtime frames. |
| EpidemicEngineSmokeTruthTests | Covers Base lifecycle, Runtime-to-Framework time projection, save/restore continuation, and complete Runtime composition. |

## Contract priorities

The assertions intentionally encode the audit four cross-cutting rules:

1. A major is the sole owner of its authoritative state.
2. Integration uses public adapters and neutral data, not peer-state mutation.
3. Reserve or prepare does not expose committed state before commit.
4. Snapshot restore validates completely before replacing live state.

Run only this source-of-truth layer with ctest --test-dir BUILD -C Debug -L truth --output-on-failure.
Run the fast end-to-end subset with label smoke, or the high-cardinality subset with label load.