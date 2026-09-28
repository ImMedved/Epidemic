# Goal 4 public-header delta review

Admission baseline: 59 EngineFramework public headers and 3054 exact public callable declarations. The integrated B01–B08 tree retains those callable IDs and signatures; `public_api_inventory.py --check` and `public_surface_manifest.py --check` validate the current inventory. Six public header files changed relative to the admission tree:

| Header | Delta and rationale | Regression evidence |
|---|---|---|
| `BaseInfrastructure/Foundation/.../type_registry.h` | Internal fault point immediately before registry publication; no callable signature changed. | `foundation_tests.cpp:272` exercises `type_registry.publish`. |
| `BaseInfrastructure/Queries/.../gameplay_queries.h` | Internal fault points before provider/freeze publication; no callable signature changed. | `queries_tests.cpp:486` and `:516` exercise both publication points. |
| `BaseInfrastructure/Facts/.../gameplay_facts.h` | Internal fault point before direct-event publication; no callable signature changed. | `facts_tests.cpp:487` exercises `direct_publish.publish`. |
| `GameplayWorldStateOwners/Simulation/.../simulation.h` | Private summary/journal storage changes from `deque` to `list` to stage then splice terminal publication without allocation. | `simulation_tests.cpp:386` starts the G4-SIM-002 publication-failure regressions. |
| `GameplayWorldStateOwners/Society/.../society.h` | Private journal changes from `deque` to pre-reserved `vector` so bounded append after authoritative mutation is non-allocating. | `society_tests.cpp:308` compares full mutable snapshot and derived queries after injected publication failure. |
| `IntegrationLayer/NarrativeIntegration/.../narrative_adapters.h` | Removes a private inline revision bump so outbox paths can preflight revision/publication in the implementation. | `narrative_integration_tests.cpp:102` defines the outbox publication-atomicity regression. |

These are source-level public-header changes, including private class-layout changes. Stable binary ABI is not a Goal 4 promise; consumers must rebuild. Debug and Release MSVC `/W4 /WX` builds, public-header self-containment, exact CTest manifests and 94/94 tests in each configuration pass locally. This review does **not** substitute for the unresolved per-callable contract/test evidence audit or remote CI on a recorded SHA.
