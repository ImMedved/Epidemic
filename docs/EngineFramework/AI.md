# AI Goal 4 B05 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns AI profiles/goals/considerations, agents, blackboards, intents, think scheduling indexes, generators, revisions and the bounded AI change journal.

The service remains the single authoritative owner of that state. Derived indexes, journals and diagnostic/read models are not independent semantic owners.

## Authoritative state and indexes

Admission/generated B05 inventory: 90 public callables, 104 mutation obligations, 4 lifecycle candidates, 26 stale-identity candidates and 0 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Multi-container mutations must complete fallible staging before authoritative publication. Deterministic query/order semantics must agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B05 coverage projection. Revision, identity-generator and journal publication is part of the mutation contract. Allocation/publication failure may not expose partially advanced primary state, index state, generator, revision or journal.

B05 allocation evidence uses source-private module seams rather than the shared process-global allocator override. The seams are implementation/test details and do not expand the public API.

## Lifecycle and identity

All emitted lifecycle and stale-identity candidates were reviewed. Invalid, removed, duplicate and stale identities are rejected or handled as the documented no-op without false publication. Retry does not silently resurrect terminal state.

## Persistence

Snapshot/restore is reviewed as candidate-state publication: validate first, build off-state, preserve the current service on failure, then atomically publish the candidate. Successful restore preserves generator/revision/cursor continuity required by the module snapshot contract.

## External boundaries

AI evaluators and target/input providers are decision inputs only. Throws/failures are isolated before authoritative agent/intent/schedule publication, with deterministic tie breaking retained.

## Threading contract

No additional internal synchronization guarantee is introduced by B05. These gameplay owners are treated as externally serialized/owner-thread services for Goal 4 local correctness. Engine-wide concurrency qualification remains Goal 6.

## Regression evidence

Registered module target: `EpidemicGameFrameworkAITests` from `EngineFramework/DevelopmentInfrastructure/Tests/ai_tests.cpp`.

Goal 4 adds source-private publication fault points around profile, agent, scheduling and think-decision/index staging. Existing evaluator failure, scheduling, deterministic choice and snapshot tests remain green.

## Goal 4 functional checklist

- [x] Profile/goal/intent/consideration registration.
- [x] Agent register/remove.
- [x] Blackboard key/value type validation.
- [x] Goal/intent lifecycle.
- [x] Think/replan.
- [x] Evaluator failure.
- [x] Access policy.
- [x] Target candidate handling.
- [x] SetNextThink scheduling.
- [x] Deterministic tie breaking.
- [x] Failed think does not half-update agent/index/journal.
- [x] Snapshot/restore agents, blackboards, intents, generators, scheduler state, journal.

## Exact public API anchors

Each row is the exact reviewed contract for one B05 callable. The matching assertion is recorded in `_goal4_handoff/B05/public_api_anchors.json`.

- `015b85bcbf0854a9` | `QUERY` | `epidemic::gameplay::ai::AIConsiderationId` | `[[nodiscard]] constexpr bool operator==(const AIConsiderationId&)const noexcept=default;`
- `01753aae8231f2e6` | `QUERY` | `epidemic::gameplay::ai::AIAccessPolicyId` | `[[nodiscard]] constexpr bool operator==(const AIAccessPolicyId&)const noexcept=default;`
- `07bc3c91de8e34bf` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(AISnapshot snapshot);`
- `07c2cb4b70e69083` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RequestReplan(GameplayObjectRef subject,AIReplanReasonId reason,GameplayTimePoint when,GameplayContext context={});`
- `0846305c8fd31cff` | `QUERY` | `epidemic::gameplay::ai::AIProfileId` | `[[nodiscard]] constexpr bool operator==(const AIProfileId&)const noexcept=default;`
- `084d0727b27264cc` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `void SetBudget(AIBudget budget)noexcept`
- `0bc1051947f7b5cd` | `QUERY` | `epidemic::gameplay::ai::AIAccessPolicyId` | `[[nodiscard]] constexpr auto operator<=>(const AIAccessPolicyId&)const noexcept=default;`
- `0bf6d3ba96a4ce6b` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] bool IsFrozen()const noexcept`
- `101ddcb39ac6651e` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<AIThinkResult> Think(GameplayObjectRef subject,const AIContextSnapshot&context,GameplayContext gameplay_context={});`
- `189b9c9db0ca7f3e` | `FACTORY` | `epidemic::gameplay::ai::AIIntentId` | `static constexpr AIIntentId FromString(std::string_view s)noexcept`
- `1e1bbb269553a74d` | `FACTORY` | `epidemic::gameplay::ai::AIIntentId` | `static constexpr AIIntentId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `1fd44e19c487bf75` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `2196c9743405074a` | `QUERY` | `epidemic::gameplay::ai::AIInputKeyId` | `[[nodiscard]] constexpr bool operator==(const AIInputKeyId&)const noexcept=default;`
- `22fbd92784893d5c` | `QUERY` | `epidemic::gameplay::ai::AIIntentId` | `[[nodiscard]] constexpr auto operator<=>(const AIIntentId&)const noexcept=default;`
- `2567d523cc89f749` | `CONSTRUCTOR` | `epidemic::gameplay::ai::AIService` | `AIService();`
- `29bc50eda13e3031` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `void SetChangeJournalCapacity(std::size_t capacity)noexcept`
- `2daeac9418433dc4` | `QUERY` | `epidemic::gameplay::ai::RefHash` | `[[nodiscard]] std::size_t operator()(GameplayObjectRef r)const noexcept`
- `3253c2311285e65a` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RegisterAgent(GameplayObjectRef subject,AIProfileId profile,GameplayTimePoint next_think={});`
- `36e16665c3c2675e` | `QUERY` | `epidemic::gameplay::ai::AIInputKeyId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `37dc74db219d5d42` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> TimeoutIntent(AIIntentId id,TypeId reason={},GameplayContext context={});`
- `3ae5157dcea29508` | `QUERY` | `epidemic::gameplay::ai::AIIntentId` | `[[nodiscard]] constexpr bool operator==(const AIIntentId&)const noexcept=default;`
- `3fd998977980862b` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `4260a3d0cd611896` | `FACTORY` | `epidemic::gameplay::ai::AIReplanReasonId` | `static constexpr AIReplanReasonId FromString(std::string_view s)noexcept`
- `4358cd3e1afb68a8` | `QUERY` | `epidemic::gameplay::ai::IAIConsiderationEvaluator` | `[[nodiscard]] virtual foundation::Result<Fixed> Evaluate(const AIConsideration&definition,const AIContextSnapshot&context,const AITargetCandidate*target)const=0;`
- `47510cb6815716d3` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] AIChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `486a6981b8b4e2d7` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] const AIAgentState*FindAgent(GameplayObjectRef subject)const noexcept;`
- `490b5ebb912fbba3` | `FACTORY` | `epidemic::gameplay::ai::AIAccessPolicyId` | `static constexpr AIAccessPolicyId FromString(std::string_view s)noexcept`
- `4946fe9b1908da00` | `QUERY` | `epidemic::gameplay::ai::BlackboardKeyId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4cf8c4aeaf7671f6` | `LIFECYCLE` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> EnableAgent(GameplayObjectRef subject,GameplayContext context={});`
- `505616a45b185be0` | `FACTORY` | `epidemic::gameplay::ai::AIConsiderationEvaluatorId` | `static constexpr AIConsiderationEvaluatorId FromString(std::string_view s)noexcept`
- `5315cf3dd54abc07` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] const AIGoalDefinition*FindGoal(AIGoalId id)const noexcept;`
- `53f9bc0e7177367d` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] const AIAccessPolicy*FindAccessPolicy(AIAccessPolicyId id)const noexcept;`
- `56991b585ee5c2ce` | `QUERY` | `epidemic::gameplay::ai::AIConsiderationEvaluatorId` | `[[nodiscard]] constexpr auto operator<=>(const AIConsiderationEvaluatorId&)const noexcept=default;`
- `577de9d4a7216c68` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] std::vector<AIAgentState> FindAgentsByActivity(AIAgentActivity activity)const;`
- `57efa798a73f044d` | `QUERY` | `epidemic::gameplay::ai::AIReplanReasonId` | `[[nodiscard]] constexpr bool operator==(const AIReplanReasonId&)const noexcept=default;`
- `598e06cb8d2f4c11` | `QUERY` | `epidemic::gameplay::ai::AIProfileId` | `[[nodiscard]] constexpr auto operator<=>(const AIProfileId&)const noexcept=default;`
- `5b9352512aedad2e` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RegisterGoal(AIGoalDefinition goal);`
- `5d315fd1f9de75e9` | `QUERY` | `epidemic::gameplay::ai::AIAccessPolicyId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `6024e65681306358` | `FACTORY` | `epidemic::gameplay::ai::AIProfileId` | `static constexpr AIProfileId FromString(std::string_view s)noexcept`
- `6ad227a95eb4a08a` | `QUERY` | `epidemic::gameplay::ai::AIInputKeyId` | `[[nodiscard]] constexpr auto operator<=>(const AIInputKeyId&)const noexcept=default;`
- `6cb9d58e6023be15` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] std::vector<AIAgentState> FindAgentsWithGoal(AIGoalId goal)const;`
- `6cbc9325519f36d9` | `QUERY` | `epidemic::gameplay::ai::AIConsiderationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `7259ca516d5e5108` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] std::vector<GameplayObjectRef> FindDueAgents(GameplayTimePoint now,std::size_t limit)const;`
- `731e3380bbfdfdc7` | `FACTORY` | `epidemic::gameplay::ai::AIIntentTypeId` | `static constexpr AIIntentTypeId FromString(std::string_view s)noexcept`
- `74957ccd60e1ca03` | `QUERY` | `epidemic::gameplay::ai::AIConsiderationEvaluatorId` | `[[nodiscard]] constexpr bool operator==(const AIConsiderationEvaluatorId&)const noexcept=default;`
- `74be39ccfb7fd3ea` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> MarkIntentFailed(AIIntentId id,TypeId reason={},GameplayContext context={});`
- `7600867f14348b13` | `FACTORY` | `epidemic::gameplay::ai::AIInputKeyId` | `static constexpr AIInputKeyId FromType(TypeId value)noexcept`
- `762d1bcb5d403102` | `DESTRUCTOR` | `epidemic::gameplay::ai::IAIConsiderationEvaluator` | `virtual ~IAIConsiderationEvaluator()=default;`
- `795b68b17ddcc9c4` | `QUERY` | `epidemic::gameplay::ai::AIReplanReasonId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `7cbe7046932f23c8` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] const AIProfile*FindProfile(AIProfileId id)const noexcept;`
- `7e7d4a4c72080ce5` | `QUERY` | `epidemic::gameplay::ai::AIReplanReasonId` | `[[nodiscard]] constexpr auto operator<=>(const AIReplanReasonId&)const noexcept=default;`
- `869ea86c96b5faad` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RemoveBlackboard(GameplayObjectRef subject,BlackboardKeyId key,GameplayContext context={});`
- `93988412a1191c07` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> MarkIntentAccepted(AIIntentId id,GameplayContext context={});`
- `9632f2de512e34e2` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `9b0e93a86a6223bd` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] const AIBlackboardEntry*FindBlackboard(GameplayObjectRef subject,BlackboardKeyId key)const noexcept;`
- `9c3e089c0270052e` | `FACTORY` | `epidemic::gameplay::ai::AIInputKeyId` | `static constexpr AIInputKeyId FromString(std::string_view s)noexcept`
- `a18d20a2699b662f` | `QUERY` | `epidemic::gameplay::ai::BlackboardKeyId` | `[[nodiscard]] constexpr bool operator==(const BlackboardKeyId&)const noexcept=default;`
- `a1b4b4813a2bdb85` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> CancelIntent(AIIntentId id,TypeId reason={},GameplayContext context={});`
- `a34946afce4bfdd9` | `LIFECYCLE` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> ResumeAgent(GameplayObjectRef subject,GameplayTimePoint when,GameplayContext context={});`
- `a62daf6fe21b743e` | `QUERY` | `epidemic::gameplay::ai` | `[[nodiscard]] constexpr std::uint32_t operator|(AIAccessFlag a,AIAccessFlag b)noexcept`
- `a6a436eea8d1825c` | `QUERY` | `epidemic::gameplay::ai::AIIntentTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `a6b5d0c1b478f2b8` | `QUERY` | `epidemic::gameplay::ai::AIIntentTypeId` | `[[nodiscard]] constexpr auto operator<=>(const AIIntentTypeId&)const noexcept=default;`
- `a7a9332ba4b487cb` | `QUERY` | `epidemic::gameplay::ai::AIProfileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `a9e87c9d543fb446` | `QUERY` | `epidemic::gameplay::ai::AIConsiderationId` | `[[nodiscard]] constexpr auto operator<=>(const AIConsiderationId&)const noexcept=default;`
- `ab1ff8bd1ccaec9c` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RegisterProfile(AIProfile profile);`
- `ac5f58dc52f148e8` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> UnregisterAgent(GameplayObjectRef subject,GameplayContext context={});`
- `b3070ade8940e604` | `QUERY` | `epidemic::gameplay::ai::AIGoalId` | `[[nodiscard]] constexpr auto operator<=>(const AIGoalId&)const noexcept=default;`
- `b7f5217272bb61e3` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RegisterAccessPolicy(AIAccessPolicy policy);`
- `b940b436ee21c694` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] static constexpr AIConsiderationEvaluatorId InputEvaluatorId()noexcept`
- `ba2684ef3fc48512` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RegisterBlackboardKey(BlackboardKeyDefinition definition);`
- `ba66a40f9bb0ca6c` | `QUERY` | `epidemic::gameplay::ai::AIGoalId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `c49c526b702f3175` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] const BlackboardKeyDefinition*FindBlackboardKey(BlackboardKeyId id)const noexcept;`
- `c6730b46ded040bd` | `QUERY` | `epidemic::gameplay::ai::BlackboardKeyId` | `[[nodiscard]] constexpr auto operator<=>(const BlackboardKeyId&)const noexcept=default;`
- `c73bb54f29eb29e2` | `FACTORY` | `epidemic::gameplay::ai::BlackboardKeyId` | `static constexpr BlackboardKeyId FromString(std::string_view s)noexcept`
- `cb2d66eccd472e59` | `QUERY` | `epidemic::gameplay::ai::AIConsiderationEvaluatorId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `cdcc42182939cb68` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] AISnapshot CaptureSnapshot()const;`
- `d27a5e3cc80be3db` | `QUERY` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] AIDiagnostics GetDiagnostics()const noexcept;`
- `de956006d6276974` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> SuspendAgent(GameplayObjectRef subject,GameplayContext context={});`
- `de9f4581cfae0981` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> SetBlackboard(GameplayObjectRef subject,BlackboardKeyId key,TypeId value_type,std::vector<std::byte> value,GameplayTimePoint now,GameplayContext context={});`
- `e01fc230f8d71952` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> MarkIntentSucceeded(AIIntentId id,GameplayContext context={});`
- `e2304ef51bea9952` | `LIFECYCLE` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> Freeze();`
- `e2e8221ddcca8f1b` | `QUERY` | `epidemic::gameplay::ai::AIIntentTypeId` | `[[nodiscard]] constexpr bool operator==(const AIIntentTypeId&)const noexcept=default;`
- `e839343053bf942b` | `QUERY` | `epidemic::gameplay::ai::AIGoalId` | `[[nodiscard]] constexpr bool operator==(const AIGoalId&)const noexcept=default;`
- `e85285bec74d24bb` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> MarkIntentRunning(AIIntentId id,GameplayContext context={});`
- `e9e04d989eda524f` | `QUERY` | `epidemic::gameplay::ai::AIIntentId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `ecccd6540a078b79` | `FACTORY` | `epidemic::gameplay::ai::AIGoalId` | `static constexpr AIGoalId FromString(std::string_view s)noexcept`
- `f5d11dc7ea131198` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> RegisterConsiderationEvaluator(AIConsiderationEvaluatorId id,const IAIConsiderationEvaluator&evaluator);`
- `f97470eb9760a8e4` | `MUTATOR` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> ScheduleThink(GameplayObjectRef subject,GameplayTimePoint when,GameplayContext context={});`
- `fd7be5f994753d08` | `LIFECYCLE` | `epidemic::gameplay::ai::AIService` | `[[nodiscard]] foundation::Result<void> DisableAgent(GameplayObjectRef subject,GameplayContext context={});`
- `ff96e875f898b191` | `FACTORY` | `epidemic::gameplay::ai::AIConsiderationId` | `static constexpr AIConsiderationId FromString(std::string_view s)noexcept`

## Local-ready projection

All 37 Goal 4 local criteria have a typed block-local `PASS` decision with exact contract/state/test evidence in `_goal4_handoff/B05/local_ready.json`. All 15 dossier fields are `REVIEWED`; canonical promotion remains the serial integrator step.
