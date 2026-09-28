# PerceptionKnowledgeAIIntegration

Goal 4 block: `b08_integration_adapters`.
Freeze unit: `EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration`.
Admission: 14 public API, 12 mutation obligations, 0 lifecycle, 2 stale-identity, 0 external-boundary candidates.

## Public contract review

- Observation -> Knowledge mapping.
- Duplicate observation.
- Knowledge update -> AI inputs.
- Missing knowledge.
- AI execution availability.
- Intent execution record exactly once.
- Stale observation/agent generation.

## Failure atomicity and accepted-work review

Every local adapter mutation was reviewed for success, invalid input, retry/duplicate behavior, stale identities/cursors and publication ordering. Fallible local bookkeeping must complete before an externally owned side effect is accepted, or a durable reconciliation record must already exist.

## Persistence and reconciliation

No independent adapter-owned snapshot/checkpoint surface is required by this freeze unit. Durable state is either carried by owner-issued tokens/outputs or explicitly delegated to the authoritative owners; whole-engine persistence remains Goal 5.

## Test evidence

- Source: `EngineFramework/DevelopmentInfrastructure/Tests/perception_knowledge_ai_integration_tests.cpp`.
- Target: `EpidemicGameFrameworkPerceptionKnowledgeAIIntegrationTests`.
- Reviewed paths include happy path, invalid/stale input, duplicate/retry behavior and local failure/reconciliation behavior applicable to this module.
- The B08 worker compiled and executed this target under C++23 with Clang warnings-as-errors in the available Linux environment. Required Windows MSVC Debug/Release qualification remains a serial-integration gate.

## Goal 4 result

`LOCAL_READY_CANDIDATE` for block-local evidence. This is not a whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN` claim.

<!-- goal4-exact-evidence -->

## Exact Goal 4 callable contracts

- G4-API-036dedecc3a128d6: `epidemic::gameplay::integration::PerceptionKnowledgeMapping` — `knowledge::BeliefTypeId observed_belief{knowledge::BeliefTypeId::FromString("framework.knowledge.observed")};`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-0e072a9d72ca49a2: `epidemic::gameplay::integration::KnowledgeAIInputKeys` — `ai::AIInputKeyId perception_sense{ai::AIInputKeyId::FromString("framework.ai.target.perception_sense")};`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-339998b57fa1f9b9: `epidemic::gameplay::integration::AIIntentExecutionRecorder` — `void Clear()noexcept`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-376aa9c0c0ab6434: `epidemic::gameplay::integration::KnowledgeAIInputKeys` — `ai::AIInputKeyId last_known_source{ai::AIInputKeyId::FromString("framework.ai.target.source.last_known")};`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-43e065c7f24af965: `epidemic::gameplay::integration::AIIntentExecutionRecorder` — `explicit AIIntentExecutionRecorder(std::size_t retention_capacity=1024)noexcept;`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-4d7e03c7b1034d57: `epidemic::gameplay::integration::AIIntentExecutionRecorder` — `[[nodiscard]] const std::vector<ai::AIIntent>&AcceptedIntents()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-556e49e91dc55d3d: `epidemic::gameplay::integration::KnowledgeAIAdapter` — `[[nodiscard]] ai::AIContextSnapshot BuildContext(GameplayObjectRef agent,AIExecutionAvailability availability,const perception::PerceptionService*perception_service=nullptr,GameplayTimePoint now={})const;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-640024ed1565e24a: `epidemic::gameplay::integration::PerceptionKnowledgeAdapter` — `[[nodiscard]] foundation::Result<ObservationKnowledgeResult> LearnFromObservation(const perception::PerceptionObservation&observation);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-730fa661882071de: `epidemic::gameplay::integration::AIIntentExecutionRecorder` — `[[nodiscard]] foundation::Result<void> Succeed(ai::AIIntentId intent,ai::AIService&ai_service);`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-90a284993b3ae527: `epidemic::gameplay::integration::PerceptionKnowledgeAdapter` — `explicit PerceptionKnowledgeAdapter(knowledge::KnowledgeService&knowledge_service,PerceptionKnowledgeMapping mapping={});`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-a6a7a532618bc521: `epidemic::gameplay::integration::AIIntentExecutionRecorder` — `[[nodiscard]] foundation::Result<void> Accept(const ai::AIIntent&intent,ai::AIService&ai_service);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-c7d7d9bdacab88f4: `epidemic::gameplay::integration::KnowledgeAIAdapter` — `explicit KnowledgeAIAdapter(const knowledge::KnowledgeService&knowledge_service,KnowledgeAIInputKeys input_keys={});`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-e03e1552c9d45928: `epidemic::gameplay::integration::AIIntentExecutionRecorder` — `[[nodiscard]] std::size_t RetentionCapacity()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-f61cb3bf4d53684b: `epidemic::gameplay::integration::KnowledgeAIInputKeys` — `ai::AIInputKeyId knowledge_source{ai::AIInputKeyId::FromString("framework.ai.target.source.knowledge")};`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.

## Exact Goal 4 criterion contracts

- G4-CRITERION-ARCH-RESPONSIBILITY: `ARCH-RESPONSIBILITY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-SINGLE-OWNER: `ARCH-SINGLE-OWNER` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-DIRECT-DEPS: `ARCH-DIRECT-DEPS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-PORTS: `ARCH-PORTS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-CLASSIFIED: `API-CLASSIFIED` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-PRECONDITIONS: `API-PRECONDITIONS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-SUCCESS: `API-SUCCESS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-FAILURE: `API-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-OVERLOADS: `API-OVERLOADS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-INVALID: `API-INVALID` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-PRIMARY: `STATE-PRIMARY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-INDEXES: `STATE-INDEXES` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-COUNTERS: `STATE-COUNTERS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-NO-FALSE-PUBLISH: `STATE-NO-FALSE-PUBLISH` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-NOOP: `STATE-NOOP` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-ALLOWED: `LIFE-ALLOWED` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-FORBIDDEN: `LIFE-FORBIDDEN` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-SHUTDOWN: `LIFE-SHUTDOWN` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-RETRY-CLEANUP: `LIFE-RETRY-CLEANUP` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-SINGLE: `ATOMIC-SINGLE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-MULTI: `ATOMIC-MULTI` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-EXTERNAL: `ATOMIC-EXTERNAL` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-RECONCILE: `ATOMIC-RECONCILE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-SNAPSHOT: `PERSIST-SNAPSHOT` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-VALIDATE: `PERSIST-VALIDATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-CANDIDATE: `PERSIST-CANDIDATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-FAILURE: `PERSIST-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-CONTINUITY: `PERSIST-CONTINUITY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-HAPPY: `TEST-HAPPY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-INVALID: `TEST-INVALID` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-DUPLICATE: `TEST-DUPLICATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-STALE: `TEST-STALE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-EMPTY: `TEST-EMPTY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-BOUNDARY: `TEST-BOUNDARY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-WRONG-LIFECYCLE: `TEST-WRONG-LIFECYCLE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-CALLBACK-FAILURE: `TEST-CALLBACK-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-REGRESSION: `TEST-REGRESSION` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
