# Knowledge Goal 4 B05 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns knowledge profile definitions, knowledge/memory records, relations/contradictions, indexes, generators, revisions and the bounded knowledge change journal.

The service remains the single authoritative owner of that state. Derived indexes, journals and diagnostic/read models are not independent semantic owners.

## Authoritative state and indexes

Admission/generated B05 inventory: 40 public callables, 48 mutation obligations, 1 lifecycle candidates, 12 stale-identity candidates and 1 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Multi-container mutations must complete fallible staging before authoritative publication. Deterministic query/order semantics must agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B05 coverage projection. Revision, identity-generator and journal publication is part of the mutation contract. Allocation/publication failure may not expose partially advanced primary state, index state, generator, revision or journal.

B05 allocation evidence uses source-private module seams rather than the shared process-global allocator override. The seams are implementation/test details and do not expand the public API.

## Lifecycle and identity

All emitted lifecycle and stale-identity candidates were reviewed. Invalid, removed, duplicate and stale identities are rejected or handled as the documented no-op without false publication. Retry does not silently resurrect terminal state.

## Persistence

Snapshot/restore is reviewed as candidate-state publication: validate first, build off-state, preserve the current service on failure, then atomically publish the candidate. Successful restore preserves generator/revision/cursor continuity required by the module snapshot contract.

## External boundaries

The admission inventory contains the block’s single external-boundary candidate in Knowledge. Provider/callback failure is reviewed as a local Result/error boundary and may not partially publish graph/index state.

## Threading contract

No additional internal synchronization guarantee is introduced by B05. These gameplay owners are treated as externally serialized/owner-thread services for Goal 4 local correctness. Engine-wide concurrency qualification remains Goal 6.

## Regression evidence

Registered module target: `EpidemicGameFrameworkKnowledgeTests` from `EngineFramework/DevelopmentInfrastructure/Tests/knowledge_memory_tests.cpp`.

Goal 4 replaces process-global restore allocation injection with deterministic source-private Knowledge restore fault points. Existing lifecycle, contradiction, decay, graph/index and snapshot regressions remain green.

## Goal 4 functional checklist

- [x] Knowledge learn/share/forget/contradict lifecycle.
- [x] Confidence/value boundaries.
- [x] Memory compaction.
- [x] Subject/source references.
- [x] Duplicate knowledge semantics.
- [x] Contradiction does not corrupt indexes.
- [x] Decay.
- [x] Graph/index consistency.
- [x] Snapshot/restore memories, relations, generators, journal.

## Exact public API anchors

Each row is the exact reviewed contract for one B05 callable. The matching assertion is recorded in `_goal4_handoff/B05/public_api_anchors.json`.

- `094f917c173c7e2a` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<KnowledgeRecordId> Learn(LearnKnowledgeRequest request);`
- `0cf80e8a814b5b2b` | `QUERY` | `epidemic::gameplay::knowledge::MemoryRecordId` | `GameplayObjectId value{};static constexpr MemoryRecordId FromString(std::string_view s)noexcept`
- `0e7ee57f976996b5` | `QUERY` | `epidemic::gameplay::knowledge::MemoryDecayRuleId` | `TypeId value{};static constexpr MemoryDecayRuleId FromString(std::string_view s)noexcept`
- `15a672fc409d1585` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] KnowledgeDiagnostics GetDiagnostics()const noexcept;`
- `2532c16ae6353bb2` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeTopic` | `[[nodiscard]] bool operator==(const KnowledgeTopic&o)const noexcept`
- `28dcf7369b273c06` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] bool DefinitionsFrozen()const noexcept`
- `29e1367013268afe` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] KnowledgeSnapshot CaptureSnapshot()const;`
- `33717e945125e4b8` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<void> RemoveProfile(GameplayObjectRef subject,GameplayContext context={});`
- `3bc483d37fbc33cd` | `QUERY` | `epidemic::gameplay::knowledge::RefHash` | `[[nodiscard]] std::size_t operator()(GameplayObjectRef r)const noexcept`
- `3e2bbef6efdd1c0f` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] const MemoryRecord*FindMemory(MemoryRecordId id)const noexcept;`
- `41d685bddb0cb107` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeTopicId` | `TypeId value{};static constexpr KnowledgeTopicId FromString(std::string_view s)noexcept`
- `4856bf373065508e` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `4961b9a57c0dbe16` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `5a728b53e93e1e33` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<KnowledgeRecordId> Share(ShareKnowledgeRequest request);`
- `5f8e54000482d92c` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeByTopic(GameplayObjectRef owner,KnowledgeTopicId topic)const;`
- `60931cfafb5f9497` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<MemoryRecordId> CreateMemory(CreateMemoryRequest request);`
- `60dce082c556dc41` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] std::vector<MemoryRecord> FindMemoriesByOwner(GameplayObjectRef owner)const;`
- `67d1b2cbe2a96f52` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<void> Forget(KnowledgeRecordId id,GameplayContext context={});`
- `6b6bb4aef4cc8a85` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] std::vector<MemoryRecord> FindImportantMemories(GameplayObjectRef owner,MemoryImportance min_importance)const;`
- `6cb930501355675d` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeByOwner(GameplayObjectRef owner)const;`
- `7683b984b34bb2a1` | `LIFECYCLE` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<void> FreezeDefinitions();`
- `927905f16d76d989` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] const MemoryDecayRule*FindDecayRule(MemoryDecayRuleId id)const noexcept;`
- `97d6a30b1ff04d1d` | `QUERY` | `epidemic::gameplay::knowledge::BeliefTypeId` | `TypeId value{};static constexpr BeliefTypeId FromString(std::string_view s)noexcept`
- `990707d1c24fad7e` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] const KnowledgeRecord*FindKnowledge(KnowledgeRecordId id)const noexcept;`
- `9b4c966caa59b0f8` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] std::optional<KnowledgeRecord> GetLastKnownPosition(GameplayObjectRef owner,GameplayObjectRef subject)const;`
- `9f3e2bc596589e54` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<MemoryRecordId> CompactMemories(GameplayObjectRef owner,std::span<const MemoryRecordId> source_ids,CreateMemoryRequest summary,GameplayContext context={});`
- `a278b856781cd36d` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<void> RegisterDecayRule(MemoryDecayRule rule);`
- `b38239fb3072f75a` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<void> CreateProfile(KnowledgeProfile profile);`
- `b9718ce16e2d32df` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeAboutSubject(GameplayObjectRef owner,GameplayObjectRef subject)const;`
- `b985bc3846842546` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `bd47b01e3eaf9533` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeSourceId` | `TypeId value{};static constexpr KnowledgeSourceId FromString(std::string_view s)noexcept`
- `c2ec558c1eb5b658` | `QUERY` | `epidemic::gameplay::knowledge::MemoryTypeId` | `TypeId value{};static constexpr MemoryTypeId FromString(std::string_view s)noexcept`
- `c5241104dd258e23` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<KnowledgeRecordId> Contradict(ContradictKnowledgeRequest request);`
- `c54bff5710ba42ce` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] const KnowledgeProfile*FindProfile(GameplayObjectRef subject)const noexcept;`
- `dfe178d67285f332` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeRecordId` | `GameplayObjectId value{};static constexpr KnowledgeRecordId FromString(std::string_view s)noexcept`
- `e0f07f2ce635ac89` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(KnowledgeSnapshot snapshot);`
- `e116a22c704584de` | `MUTATOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] foundation::Result<void> Decay(GameplayTimePoint now);`
- `e1e21635deed8247` | `CONSTRUCTOR` | `epidemic::gameplay::knowledge::KnowledgeService` | `explicit KnowledgeService(std::size_t change_capacity=4096);`
- `ed84c8ad2ae74c80` | `QUERY` | `epidemic::gameplay::knowledge::KnowledgeService` | `[[nodiscard]] KnowledgeChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `f6a3e6889e0c8c46` | `QUERY` | `epidemic::gameplay::knowledge::IdHash` | `template<class T> [[nodiscard]] std::size_t operator()(const T&id)const noexcept`

## Local-ready projection

All 37 Goal 4 local criteria have a typed block-local `PASS` decision with exact contract/state/test evidence in `_goal4_handoff/B05/local_ready.json`. All 15 dossier fields are `REVIEWED`; canonical promotion remains the serial integrator step.
