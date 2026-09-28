# EngineFramework contract documentation

`EngineFramework` is the gameplay-semantic layer above `EngineBase` and `EngineRuntime`. Goal 4 qualifies each production module for `LOCAL_READY`; this documentation must not claim whole-engine `SYSTEM_READY` or `FROZEN` before Goals 5-9.

## Documentation convention

Each production freeze unit owns one document in this directory or in `modules/`. A module document records responsibility, authoritative and derived state, direct dependencies, public mutation/query surface, identity/lifecycle rules, persistence boundary, external ports, limits, threading contract, local invariants, and current Goal 4 status. Contract text describes the current headers and tests rather than copying stale roadmap prose.

Canonical generated evidence under `docs/freeze/` remains owned by the serial integrator. Worker deltas place reviewed dossier, coverage, API-anchor and defect metadata under `_goal4_handoff/Bxx/`.

Current canonical status (2026-09-28): `docs/freeze/local_ready_ledger.json` has 52/52 Framework modules `LOCAL_READY` and 0 `BLOCKED`. `python docs/freeze/goal4_evidence_quality.py --check` and all canonical validator self-tests pass after serial merge. The quality gate also rejects setup-only/include/type anchors as sole test evidence, and direct module-local regressions cover API surfaces that previously lacked an executable anchor. This is module-local readiness only; it does not claim whole-engine `SYSTEM_READY` or `FROZEN`. Final exact-tree Windows/MSVC serial qualification and remote CI are recorded separately.

## Module documents in `modules/`

- [Foundation](modules/foundation.md)
- [SupportRandom](modules/support_random.md)
- [Queries](modules/queries.md)
- [Facts](modules/facts.md)
- [Time](modules/time.md)
- [RuntimeBridge](modules/runtime_bridge.md)
- [World](modules/world.md)
- [SaveGame](modules/save_game.md)
- [Narrative](modules/narrative.md)
- [Dialogue](modules/dialogue.md)
- [NarrativeIntegration](modules/narrative_integration.md)
- [WorldIntegration](modules/world_integration.md)

## Module documents in this directory

- [Entities](Entities.md), [Materials](Materials.md), [Environment](Environment.md), [Conditions](Conditions.md), [Effects](Effects.md), [Interaction](Interaction.md), [Ownership](Ownership.md)
- [ItemsInventory](ItemsInventory.md), [Equipment](Equipment.md), [Economy](Economy.md), [Processes](Processes.md), [ResourcesProduction](ResourcesProduction.md), [Loot](Loot.md)
- [RolesJobs](RolesJobs.md), [NeedsLife](NeedsLife.md), [Population](Population.md), [Encounters](Encounters.md), [Society](Society.md), [Crime](Crime.md)
- [Perception](Perception.md), [Knowledge](Knowledge.md), [NavigationSemantics](NavigationSemantics.md), [AI](AI.md), [Simulation](Simulation.md)
- [Combat](Combat.md), [Abilities](Abilities.md), [Progression](Progression.md), [Construction](Construction.md), [Traversal](Traversal.md)
- [Integration](Integration.md), [StateIntegration](StateIntegration.md), [InteractionTimeIntegration](InteractionTimeIntegration.md), [InteractionEffectsIntegration](InteractionEffectsIntegration.md), [GameplayIntegration](GameplayIntegration.md), [ExtendedGameplayIntegration](ExtendedGameplayIntegration.md)
- [PerceptionKnowledgeAIIntegration](PerceptionKnowledgeAIIntegration.md), [PopulationSimulationIntegration](PopulationSimulationIntegration.md), [ProcessResourceSimulationIntegration](ProcessResourceSimulationIntegration.md), [SocialLegalIntegration](SocialLegalIntegration.md), [TraversalNavigationConstructionIntegration](TraversalNavigationConstructionIntegration.md)
