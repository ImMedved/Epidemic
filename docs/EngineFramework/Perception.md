# Perception Goal 4 B05 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns perception definitions, perceivers, stimuli, pending/active observations, awareness state, spatial evaluation policy, generators, revisions and the bounded perception change journal.

The service remains the single authoritative owner of that state. Derived indexes, journals and diagnostic/read models are not independent semantic owners.

## Authoritative state and indexes

Admission/generated B05 inventory: 72 public callables, 56 mutation obligations, 1 lifecycle candidates, 13 stale-identity candidates and 0 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Multi-container mutations must complete fallible staging before authoritative publication. Deterministic query/order semantics must agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B05 coverage projection. Revision, identity-generator and journal publication is part of the mutation contract. Allocation/publication failure may not expose partially advanced primary state, index state, generator, revision or journal.

B05 allocation evidence uses source-private module seams rather than the shared process-global allocator override. The seams are implementation/test details and do not expand the public API.

## Lifecycle and identity

All emitted lifecycle and stale-identity candidates were reviewed. Invalid, removed, duplicate and stale identities are rejected or handled as the documented no-op without false publication. Retry does not silently resurrect terminal state.

## Persistence

Snapshot/restore is reviewed as candidate-state publication: validate first, build off-state, preserve the current service on failure, then atomically publish the candidate. Successful restore preserves generator/revision/cursor continuity required by the module snapshot contract.

## External boundaries

Perceiver sample providers and custom evaluators are read/evaluation ports. Their failure is translated to Result/error semantics before observation/index/revision publication.

## Threading contract

No additional internal synchronization guarantee is introduced by B05. These gameplay owners are treated as externally serialized/owner-thread services for Goal 4 local correctness. Engine-wide concurrency qualification remains Goal 6.

## Regression evidence

Registered module target: `EpidemicGameFrameworkPerceptionTests` from `EngineFramework/DevelopmentInfrastructure/Tests/perception_tests.cpp`.

`G4-PER-001` and `G4-PER-003` are covered by `TestSpatialBoundaryMath`; `G4-PER-002` is covered by `TestDecayBoundaryArithmetic`. Exact coordinate deltas are formed before floating conversion, squared range comparison uses portable wide arithmetic, linear distance no longer narrows through signed 64-bit squared distance, and awareness interval advancement uses integer arithmetic.

## Goal 4 functional checklist

- [x] Sense, perceiver-profile and evaluator definition registration/freeze.
- [x] Stimulus lifecycle and perceiver sampling/provider contracts.
- [x] Observation and awareness lifecycle, duplicate/coalescing semantics and deterministic ordering.
- [x] Built-in spatial evaluation: range, attenuation and FOV with exact handling of large int64 millimetre coordinates.
- [x] Provider/evaluator failure leaves observations, indexes, revision and journal unchanged.
- [x] Expiration, forgetting, suspicion/awareness decay and large catch-up intervals.
- [x] Materialized/runtime-projection flags and stale subject/stimulus references.
- [x] Revision/change-sequence exhaustion and bounded journal semantics.
- [x] Snapshot/restore of definitions where applicable, stimuli, observations, awareness state, generators, indexes and journal.

## Exact public API anchors

Each row is the exact reviewed contract for one B05 callable. The matching assertion is recorded in `_goal4_handoff/B05/public_api_anchors.json`.

- `0324b9a9dd92a26c` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] std::vector<PerceptionObservation> FindActiveObservations(GameplayObjectRef perceiver,GameplayTimePoint now)const;`
- `0482bd415d4ff58c` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<PerceptionStimulusId> CreateStimulus(PerceptionStimulus stimulus);`
- `0a41b5992ee7a386` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] const SenseDefinition*FindSense(SenseTypeId id)const noexcept;`
- `0ae1765abbaac232` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<void> RegisterPerceiver(GameplayObjectRef subject,PerceiverProfileId profile,GameplayContext context={});`
- `0c4e66d3934e1f0c` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<void> ExpireStimuli(GameplayTimePoint now,GameplayContext context={});`
- `0ca50c3d0dd26c3b` | `QUERY` | `epidemic::gameplay::perception::WorldPosition` | `[[nodiscard]] constexpr auto operator<=>(const WorldPosition&)const noexcept=default;`
- `0cbb85ddd752c1c7` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `17ea0259da9175da` | `QUERY` | `epidemic::gameplay::perception::PerceiverProfileId` | `[[nodiscard]] constexpr auto operator<=>(const PerceiverProfileId&)const noexcept=default;`
- `1947f9a750aadef6` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] PerceptionDiagnostics GetDiagnostics()const noexcept;`
- `1bb2054bbe025493` | `QUERY` | `epidemic::gameplay::perception::PerceptionStimulusId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `1bd4b1dd9c9499d9` | `FACTORY` | `epidemic::gameplay::perception::PerceiverProfileId` | `static constexpr PerceiverProfileId FromString(std::string_view s)noexcept`
- `2590378da3402de6` | `QUERY` | `epidemic::gameplay::perception::SenseTypeId` | `[[nodiscard]] constexpr auto operator<=>(const SenseTypeId&)const noexcept=default;`
- `265826bdd32aa35e` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `269fcb8d25d2ba8b` | `CONSTRUCTOR` | `epidemic::gameplay::perception::PerceptionService` | `PerceptionService();`
- `2a4594b42185b25e` | `QUERY` | `epidemic::gameplay::perception::PerceptionObservationId` | `[[nodiscard]] constexpr auto operator<=>(const PerceptionObservationId&)const noexcept=default;`
- `2ef9f460f2fef338` | `QUERY` | `epidemic::gameplay::perception::RefHash` | `[[nodiscard]] std::size_t operator()(GameplayObjectRef r)const noexcept`
- `2efcefc4184a6379` | `QUERY` | `epidemic::gameplay::perception::WorldDirection` | `[[nodiscard]] constexpr bool operator==(const WorldDirection&)const noexcept=default;`
- `40c43018f17476c6` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(PerceptionSnapshot snapshot);`
- `415c35949d8fc1e7` | `FACTORY` | `epidemic::gameplay::perception::PerceptionStimulusId` | `static constexpr PerceptionStimulusId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `472595296984efb3` | `QUERY` | `epidemic::gameplay::perception::PerceiverProfileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4a0ab70b5a4dec7a` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<void> RegisterSense(SenseDefinition definition);`
- `4c3e2e8239c4bccb` | `QUERY` | `epidemic::gameplay::perception::AwarenessStateId` | `[[nodiscard]] constexpr auto operator<=>(const AwarenessStateId&)const noexcept=default;`
- `512129d7970c6f0e` | `QUERY` | `epidemic::gameplay::perception::AwarenessKeyHash` | `[[nodiscard]] std::size_t operator()(const AwarenessKey&k)const noexcept`
- `5430039b4f531721` | `FACTORY` | `epidemic::gameplay::perception::PerceptionObservationId` | `static constexpr PerceptionObservationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `6277dfed87d03d75` | `FACTORY` | `epidemic::gameplay::perception::SenseTypeId` | `static constexpr SenseTypeId FromString(std::string_view s)noexcept`
- `639374a41089f984` | `DESTRUCTOR` | `epidemic::gameplay::perception::ISenseEvaluator` | `virtual ~ISenseEvaluator()=default;`
- `640fc4bab4539be4` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<void> RegisterEvaluator(SenseEvaluatorId id,std::shared_ptr<const ISenseEvaluator> evaluator);`
- `67ddf01d004791a3` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] PerceptionChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `686d357e66abc06c` | `QUERY` | `epidemic::gameplay::perception::PerceptionStimulusId` | `[[nodiscard]] constexpr bool operator==(const PerceptionStimulusId&)const noexcept=default;`
- `6e4991844f46c3d2` | `QUERY` | `epidemic::gameplay::perception::SenseTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `6edb72f46d15f065` | `QUERY` | `epidemic::gameplay::perception::SenseTypeId` | `[[nodiscard]] constexpr bool operator==(const SenseTypeId&)const noexcept=default;`
- `7135e0a93c76c0e9` | `FACTORY` | `epidemic::gameplay::perception::SenseEvaluatorId` | `static constexpr SenseEvaluatorId FromString(std::string_view s)noexcept`
- `74f83491de15541a` | `QUERY` | `epidemic::gameplay::perception::PerceptionObservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `75d169060b4820ad` | `QUERY` | `epidemic::gameplay::perception::ISenseEvaluator` | `[[nodiscard]] virtual foundation::Result<SenseEvaluationResult> Evaluate(const SenseEvaluationInput&input)const=0;`
- `7a14a5a0464ec8ba` | `FACTORY` | `epidemic::gameplay::perception::AwarenessStateId` | `static constexpr AwarenessStateId FromString(std::string_view s)noexcept`
- `7a5b2fab5f058258` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] VisibilityResult EvaluateVisibility(GameplayObjectRef perceiver,GameplayObjectRef target,const PerceiverEvaluationSample&sample,WorldPosition target_position,Fixed stimulus_strength_micro=1'000'000)const;`
- `7b9b36e3c511ff90` | `QUERY` | `epidemic::gameplay::perception::SenseEvaluatorId` | `[[nodiscard]] constexpr bool operator==(const SenseEvaluatorId&)const noexcept=default;`
- `824d36194824fdf2` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<void> RegisterProfileDefinition(PerceiverProfileDefinition definition);`
- `950a6c8cf919e5b2` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] PerceptionSnapshot CaptureSnapshot()const;`
- `97eab5d7639d51c5` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] std::vector<PerceptionStimulus> FindStimuliInArea(GameplayObjectRef area)const;`
- `a1e3f8546bfdae9c` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] AudibilityResult EvaluateAudibility(GameplayObjectRef perceiver,PerceptionStimulusId stimulus,const PerceiverEvaluationSample&sample)const;`
- `a29605f41375258a` | `QUERY` | `epidemic::gameplay::perception::PendingKey` | `[[nodiscard]] constexpr auto operator<=>(const PendingKey&)const noexcept=default;`
- `a34bbb409659aadc` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `abbd860ef5be7b97` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] const PerceiverProfileDefinition*FindProfileDefinition(PerceiverProfileId id)const noexcept;`
- `b26cd4861a55946e` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] const AwarenessRecord*GetAwareness(GameplayObjectRef perceiver,GameplayObjectRef target)const noexcept;`
- `b8328e084de0d2e8` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] std::vector<PerceptionObservation> FindObservationsByPerceiver(GameplayObjectRef perceiver)const;`
- `bb190480b150af64` | `CONSTRUCTOR` | `epidemic::gameplay::perception::PerceptionProcessingContext` | `PerceptionProcessingContext()=default;`
- `bbb5603e7e6ff8a1` | `QUERY` | `epidemic::gameplay::perception::PerceptionObservationId` | `[[nodiscard]] constexpr bool operator==(const PerceptionObservationId&)const noexcept=default;`
- `bdfd48e6baac913d` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `void SetTemporalPolicy(PerceptionTemporalPolicy policy)noexcept`
- `be006df06681e51a` | `CONSTRUCTOR` | `epidemic::gameplay::perception::PerceptionProcessingContext` | `explicit PerceptionProcessingContext(GameplayTickId t,GameplayTimePoint n,GameplayContext g={},std::vector<PerceiverEvaluationSample> samples={})`
- `be06d995400c163c` | `QUERY` | `epidemic::gameplay::perception::PerceptionProcessingContext` | `[[nodiscard]] const PerceiverEvaluationSample*FindPerceiver(GameplayObjectRef subject)const noexcept`
- `c26c38e1d18313b8` | `QUERY` | `epidemic::gameplay::perception::PendingKey` | `[[nodiscard]] constexpr bool operator==(const PendingKey&)const noexcept=default;`
- `c458adb558d17381` | `QUERY` | `epidemic::gameplay::perception::AwarenessStateId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `cb75af5bc3a7e570` | `FACTORY` | `epidemic::gameplay::perception::PerceptionStimulusId` | `static constexpr PerceptionStimulusId FromString(std::string_view s)noexcept`
- `cba687034c9a65b7` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `void SetChangeJournalCapacity(std::size_t capacity)noexcept`
- `cd4e0a7f1fefb6b0` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<std::vector<PerceptionObservation>> AdvanceTime(GameplayTimePoint now,GameplayContext context={});`
- `ce00d9f70da1c0f3` | `QUERY` | `epidemic::gameplay::perception::AwarenessStateId` | `[[nodiscard]] constexpr bool operator==(const AwarenessStateId&)const noexcept=default;`
- `cf0e28dd96d4847a` | `QUERY` | `epidemic::gameplay::perception::PerceiverProfileId` | `[[nodiscard]] constexpr bool operator==(const PerceiverProfileId&)const noexcept=default;`
- `cf9bd45b41abbd31` | `QUERY` | `epidemic::gameplay::perception::WorldPosition` | `[[nodiscard]] constexpr bool operator==(const WorldPosition&)const noexcept=default;`
- `d268bde53dc51ad2` | `QUERY` | `epidemic::gameplay::perception::SenseEvaluatorId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d3ed33f10161a663` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] const PerceptionStimulus*FindStimulus(PerceptionStimulusId id)const noexcept;`
- `e8ce032a10c3e362` | `LIFECYCLE` | `epidemic::gameplay::perception::PerceptionService` | `void Freeze()noexcept`
- `eb8ea22fe0c8d83b` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<void> UnregisterPerceiver(GameplayObjectRef subject,GameplayContext context={});`
- `ee4e943a6c826d05` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] bool IsFrozen()const noexcept`
- `ef869c11f7ed4f3b` | `QUERY` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] const PerceiverRecord*FindPerceiver(GameplayObjectRef subject)const noexcept;`
- `f1c3e4459a30b0bf` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `void SetBudget(PerceptionBudget budget)noexcept`
- `f2874cc0d131fc88` | `FACTORY` | `epidemic::gameplay::perception::PerceptionObservationId` | `static constexpr PerceptionObservationId FromString(std::string_view s)noexcept`
- `f620922cb6b2283e` | `QUERY` | `epidemic::gameplay::perception::AwarenessKey` | `[[nodiscard]] constexpr bool operator==(const AwarenessKey&)const noexcept=default;`
- `f6748b666a448828` | `QUERY` | `epidemic::gameplay::perception::AwarenessKey` | `[[nodiscard]] constexpr auto operator<=>(const AwarenessKey&)const noexcept=default;`
- `f6d9f6532dea921e` | `QUERY` | `epidemic::gameplay::perception::PerceptionStimulusId` | `[[nodiscard]] constexpr auto operator<=>(const PerceptionStimulusId&)const noexcept=default;`
- `f8380abc34cd357f` | `QUERY` | `epidemic::gameplay::perception::SenseEvaluatorId` | `[[nodiscard]] constexpr auto operator<=>(const SenseEvaluatorId&)const noexcept=default;`
- `ff971f56883e1796` | `MUTATOR` | `epidemic::gameplay::perception::PerceptionService` | `[[nodiscard]] foundation::Result<std::vector<PerceptionObservation>> ProcessStimulus(PerceptionStimulusId stimulus,const PerceptionProcessingContext&context);`

## Local-ready projection

All 37 Goal 4 local criteria have a typed block-local `PASS` decision with exact contract/state/test evidence in `_goal4_handoff/B05/local_ready.json`. All 15 dossier fields are `REVIEWED`; canonical promotion remains the serial integrator step.
