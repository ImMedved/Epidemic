# Processes Goal 4 B03 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns process definitions, recipes, stations, process instances, reservation/output reconciliation records, timing state, revisions/generators and the process journal.

The service remains the single authoritative owner of the state listed below. Derived indexes and journals are not independent semantic owners.

## Authoritative state and indexes

Admission inventory: 93 public callables, 88 mutation obligations, 4 lifecycle candidates, 20 stale-identity candidates and 4 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Mutations that span several containers must complete all fallible staging before the no-fail authoritative commit point. Query ordering and index-derived views are required to agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B03 coverage projection. Revision, ID-generator and journal publication is part of the mutation contract. Allocation/publication failure must not expose a partially advanced generator, revision, primary record, derived index or journal entry.

B03 allocation evidence uses source-private module seams rather than the shared process-global allocator override. These seams are test-only implementation details and do not expand the public API.

## Lifecycle and identity

Lifecycle candidates and stale identity candidates from the admission inventory were reviewed. Invalid, removed, duplicate and stale IDs are required to be rejected or treated as the documented no-op without publishing false state. Terminal records are not silently resurrected by retry.

## Persistence

Snapshot/restore is reviewed as a candidate-state operation: validate first, build off-state, preserve all live state on failure, then publish the candidate. Successful restore preserves the persistent ID-generator/revision/journal boundary represented by the snapshot contract.

## External boundaries

Input and output providers are transactional ports. Prepare, commit, cancel, compensation and retry/reconciliation paths are part of the local contract. Four admission external-boundary candidates were reviewed.

External callback/provider failure is converted to the module error/result contract. Retry must not duplicate an already committed semantic side effect, and failed compensation that requires reconciliation stays represented by owned state.

## Threading contract

No additional internal synchronization guarantee is introduced by B03. These owners are treated as externally serialized/owner-thread services for local correctness evidence. Later Goal 6 performs the engine-wide concurrency qualification.

## Regression evidence

Registered module target: `EpidemicGameFrameworkProcessesTests` from `EngineFramework/DevelopmentInfrastructure/Tests/processes_tests.cpp`.


`G4-PROC-001` is closed by exact integer progress scaling and `TestExactProgressScaling`. The large `INT64_MAX` one-tick-before-completion case yields 999999 without relying on extended floating-point precision.

## Goal 4 functional checklist

- [x] Process definition/execution lifecycle.
- [x] Input reservations.
- [x] Output prepare/commit.
- [x] Cancellation.
- [x] Provider failure.
- [x] Partial input/output failure rollback.
- [x] Process scheduling/state transitions.
- [x] Snapshot/restore executions, reservations, generators, journal.

## Exact public API anchors

Each row is the block-local contract anchor for one admission callable. The matching test anchor is stored in `_goal4_handoff/B03/public_api_anchors.json`.

- `024eeb81ae9dc351` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] const ProcessRecipe*FindRecipe(ProcessRecipeId id)const noexcept;`
- `05ffe91a0df45785` | `QUERY` | `epidemic::gameplay::processes::ProcessRecipeId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessRecipeId&)const noexcept=default;`
- `08ebcdfc2bd143bd` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] ProcessesDiagnostics GetDiagnostics()const noexcept;`
- `0a114f0714b996f8` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `void SetQualityProvider(const IProcessQualityProvider*provider)noexcept`
- `1431d022d64dd5b6` | `FACTORY` | `epidemic::gameplay::processes::ProcessInputTypeId` | `static constexpr ProcessInputTypeId FromString(std::string_view s)noexcept`
- `15371e694b60ecc9` | `QUERY` | `epidemic::gameplay::processes::ProcessStationId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessStationId&)const noexcept=default;`
- `1da7715f85722602` | `FACTORY` | `epidemic::gameplay::processes::ProcessStationId` | `static constexpr ProcessStationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `1facdd0130f0d73a` | `QUERY` | `epidemic::gameplay::processes::IProcessOutputHandler` | `[[nodiscard]] virtual bool Supports(ProcessOutputTypeId type)const noexcept=0;`
- `216da3819b34651a` | `MUTATOR` | `epidemic::gameplay::processes::IProcessInputProvider` | `[[nodiscard]] virtual foundation::Result<ReservedProcessInput> Reserve(const ProcessInputDefinition&input,const StartProcessRequest&request,ProcessInstanceId instance)=0;`
- `2241273839b78e98` | `QUERY` | `epidemic::gameplay::processes::ProcessKindId` | `[[nodiscard]] constexpr bool operator==(const ProcessKindId&)const noexcept=default;`
- `228a2ad343d27f91` | `QUERY` | `epidemic::gameplay::processes::ProcessInputId` | `[[nodiscard]] constexpr bool operator==(const ProcessInputId&)const noexcept=default;`
- `22e5847b7ecb07fe` | `FACTORY` | `epidemic::gameplay::processes::ProcessOutputId` | `static constexpr ProcessOutputId FromString(std::string_view s)noexcept`
- `24d4bb27f00f41f7` | `QUERY` | `epidemic::gameplay::processes::ProcessStationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `2be3fd8d9574991a` | `QUERY` | `epidemic::gameplay::processes::ProcessInstanceId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessInstanceId&)const noexcept=default;`
- `3761cc75d7a97483` | `FACTORY` | `epidemic::gameplay::processes::ProcessRecipeId` | `static constexpr ProcessRecipeId FromString(std::string_view s)noexcept`
- `391e2dc37d6184fd` | `QUERY` | `epidemic::gameplay::processes::ProcessOutputId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessOutputId&)const noexcept=default;`
- `3938a8723634cdea` | `QUERY` | `epidemic::gameplay::processes::ProcessReservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `42b809c9a8d1f9bf` | `QUERY` | `epidemic::gameplay::processes::ProcessDefinitionId` | `[[nodiscard]] constexpr bool operator==(const ProcessDefinitionId&)const noexcept=default;`
- `44d693765c4f20dc` | `FACTORY` | `epidemic::gameplay::processes::ProcessQualityId` | `static constexpr ProcessQualityId FromString(std::string_view s)noexcept`
- `4946416630739918` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<void> PruneTerminalProcesses(std::size_t keep_recent=0);`
- `4e929ee1d06bebcf` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<ProcessStationId> RegisterStation(ProcessStation station);`
- `4ef1bb517ad39fd8` | `QUERY` | `epidemic::gameplay::processes::ProcessStepId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessStepId&)const noexcept=default;`
- `50d85c76c90a9f82` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<ProcessBatchCompletionReport> CompletePreparedDueForSimulation(GameplayObjectRef simulation_area,std::span<const ProcessInstanceId> process_ids,GameplayTimePoint now,GameplayContext context={});`
- `5262c859c49ce62a` | `QUERY` | `epidemic::gameplay::processes::ProcessRecipeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `5547127f5f479652` | `FACTORY` | `epidemic::gameplay::processes::ProcessStepId` | `static constexpr ProcessStepId FromString(std::string_view s)noexcept`
- `5579449d8cd35a8c` | `FACTORY` | `epidemic::gameplay::processes::ProcessInputId` | `static constexpr ProcessInputId FromString(std::string_view s)noexcept`
- `5a74ef04371f21b7` | `QUERY` | `epidemic::gameplay::processes::ProcessInputId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessInputId&)const noexcept=default;`
- `5b26a31b28e8b091` | `LIFECYCLE` | `epidemic::gameplay::processes::ProcessesService` | `void Freeze()noexcept`
- `5bbcc534973a4d45` | `QUERY` | `epidemic::gameplay::processes::ProcessStepId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `602a9cf682ea9424` | `MUTATOR` | `epidemic::gameplay::processes::IProcessInputProvider` | `[[nodiscard]] virtual foundation::Result<void> Release(const ReservedProcessInput&reservation,GameplayContext context)=0;`
- `61e996be1f191b33` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<void> Cancel(ProcessInstanceId id,GameplayTimePoint now,GameplayContext context={});`
- `63e74858510bfca1` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `void SetInputProvider(IProcessInputProvider*provider)noexcept`
- `6566f63f135147ea` | `QUERY` | `epidemic::gameplay::processes::ProcessReservationId` | `[[nodiscard]] constexpr bool operator==(const ProcessReservationId&)const noexcept=default;`
- `6b13f833e3fd511d` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] ProcessesSnapshot CaptureSnapshot()const;`
- `6b89f13f69b4d357` | `MUTATOR` | `epidemic::gameplay::processes::IProcessOutputHandler` | `[[nodiscard]] virtual foundation::Result<void> Cancel(const PreparedProcessOutput&output,const ProcessInstance&instance,GameplayContext context)=0;`
- `6ce19ebd1dd594e7` | `QUERY` | `epidemic::gameplay::processes::ProcessOutputTypeId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessOutputTypeId&)const noexcept=default;`
- `6e31c76fcab6b979` | `QUERY` | `epidemic::gameplay::processes::ProcessInputTypeId` | `[[nodiscard]] constexpr bool operator==(const ProcessInputTypeId&)const noexcept=default;`
- `7150a4b6aa447f9f` | `FACTORY` | `epidemic::gameplay::processes::ProcessInstanceId` | `static constexpr ProcessInstanceId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `71a7a300d10de8c9` | `QUERY` | `epidemic::gameplay::processes::ProcessInputTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `725ec67192f2c945` | `DESTRUCTOR` | `epidemic::gameplay::processes::IProcessInputProvider` | `virtual ~IProcessInputProvider()=default;`
- `808a7c5c338f3e7a` | `FACTORY` | `epidemic::gameplay::processes::ProcessDefinitionId` | `static constexpr ProcessDefinitionId FromString(std::string_view s)noexcept`
- `8230cb80512b7633` | `MUTATOR` | `epidemic::gameplay::processes::IProcessOutputHandler` | `[[nodiscard]] virtual foundation::Result<PreparedProcessOutput> Prepare(const ProcessOutputDefinition&output,const ProcessInstance&instance,GameplayContext context)=0;`
- `845178c4aa982cee` | `QUERY` | `epidemic::gameplay::processes::ProcessQualityId` | `[[nodiscard]] constexpr bool operator==(const ProcessQualityId&)const noexcept=default;`
- `85e64f37945f4911` | `QUERY` | `epidemic::gameplay::processes::ProcessKindId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `884411043493d349` | `LIFECYCLE` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<void> Pause(ProcessInstanceId id,GameplayTimePoint now,GameplayContext context={});`
- `8945b663cb8f07cc` | `QUERY` | `epidemic::gameplay::processes::IProcessInputProvider` | `[[nodiscard]] virtual foundation::Result<void> Validate(const ProcessInputDefinition&input,const StartProcessRequest&request,ProcessInstanceId instance)=0;`
- `8ad70988cf8d6c8d` | `QUERY` | `epidemic::gameplay::processes::ProcessBatchCompletionReport` | `[[nodiscard]] bool AllCompleted()const noexcept`
- `8dbe0251e72bb5c4` | `MUTATOR` | `epidemic::gameplay::processes::IProcessOutputHandler` | `[[nodiscard]] virtual foundation::Result<void> Commit(const PreparedProcessOutput&output,const ProcessInstance&instance,GameplayContext context)=0;`
- `9067cd4a3a3cf59e` | `LIFECYCLE` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<ProcessInstanceId> StartProcess(StartProcessRequest request);`
- `90b9c72d1e0b5d21` | `QUERY` | `epidemic::gameplay::processes::RegisteredPayload` | `[[nodiscard]] bool IsPortable()const noexcept`
- `94e3fb851840c16f` | `QUERY` | `epidemic::gameplay::processes::ProcessDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessDefinitionId&)const noexcept=default;`
- `95f7743356ed0e93` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] const ProcessInstance*FindInstance(ProcessInstanceId id)const noexcept;`
- `97cd460ba5729de5` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] std::vector<ProcessInstance> FindDueProcessesForSimulation(GameplayObjectRef simulation_area,GameplayTimePoint from,GameplayTimePoint to)const;`
- `9877363f6ae0bca4` | `QUERY` | `epidemic::gameplay::processes::ProcessOutputId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `a1f917b4e5f1caf6` | `QUERY` | `epidemic::gameplay::processes::ProcessOutputId` | `[[nodiscard]] constexpr bool operator==(const ProcessOutputId&)const noexcept=default;`
- `a74c9eac1f3d209b` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] Fixed EvaluateProgress(ProcessInstanceId id,GameplayTimePoint now)const noexcept;`
- `a7a975eabb9f9778` | `QUERY` | `epidemic::gameplay::processes::ProcessStationId` | `[[nodiscard]] constexpr bool operator==(const ProcessStationId&)const noexcept=default;`
- `a91d19e0adc00dbb` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<ProcessRecipeId> RegisterRecipe(ProcessRecipe recipe);`
- `ab3f0bd334b38a10` | `MUTATOR` | `epidemic::gameplay::processes::IProcessInputProvider` | `[[nodiscard]] virtual foundation::Result<void> Consume(const ReservedProcessInput&reservation,GameplayContext context)=0;`
- `aee2a675d9c9f87a` | `QUERY` | `epidemic::gameplay::processes::ProcessDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `b0f746c3ecf3ad3d` | `DESTRUCTOR` | `epidemic::gameplay::processes::IProcessQualityProvider` | `virtual ~IProcessQualityProvider()=default;`
- `b2556b5e26717aaa` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `void AddOutputHandler(IProcessOutputHandler*handler)`
- `b5eef55971bdda4e` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] const ProcessStation*FindStationByObject(GameplayObjectRef station)const noexcept;`
- `b74666ef41d2be83` | `QUERY` | `epidemic::gameplay::processes::ProcessQualityId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `b78db7edc6999664` | `QUERY` | `epidemic::gameplay::processes::ProcessReservationId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessReservationId&)const noexcept=default;`
- `b84647c0a9030c3f` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `b87a006e689b9125` | `FACTORY` | `epidemic::gameplay::processes::ProcessKindId` | `static constexpr ProcessKindId FromString(std::string_view s)noexcept`
- `b8df868997797fb4` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(ProcessesSnapshot snapshot);`
- `ba9ca99abb18296e` | `QUERY` | `epidemic::gameplay::processes::ProcessInputTypeId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessInputTypeId&)const noexcept=default;`
- `c08ecfb5a5d30a55` | `FACTORY` | `epidemic::gameplay::processes::RegisteredPayload` | `[[nodiscard]] static RegisteredPayload FromVersioned(TypeId type_id,std::uint32_t version,std::vector<std::byte> encoded)`
- `c6dd3ec688bc2fa2` | `FACTORY` | `epidemic::gameplay::processes::ProcessOutputTypeId` | `static constexpr ProcessOutputTypeId FromString(std::string_view s)noexcept`
- `c754f71660defa79` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] ProcessChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `cac14280c01686f7` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] const ProcessDefinition*FindDefinition(ProcessDefinitionId id)const noexcept;`
- `cb7dd50d14d249ff` | `DESTRUCTOR` | `epidemic::gameplay::processes::IProcessOutputHandler` | `virtual ~IProcessOutputHandler()=default;`
- `ccd5661a9c0ee98c` | `QUERY` | `epidemic::gameplay::processes::ProcessKindId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessKindId&)const noexcept=default;`
- `d1831b3490268bce` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<ProcessDefinitionId> RegisterDefinition(ProcessDefinition definition);`
- `d617c840fdbfafab` | `FACTORY` | `epidemic::gameplay::processes::ProcessReservationId` | `static constexpr ProcessReservationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `d9674dec915dc66d` | `QUERY` | `epidemic::gameplay::processes::ProcessOutputTypeId` | `[[nodiscard]] constexpr bool operator==(const ProcessOutputTypeId&)const noexcept=default;`
- `dca9114b40adbf80` | `LIFECYCLE` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<void> Resume(ProcessInstanceId id,GameplayTimePoint now,GameplayContext context={});`
- `e37ebe51819dbbe5` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<void> Complete(ProcessInstanceId id,GameplayTimePoint now,GameplayContext context={});`
- `e45d147cfea28bcd` | `QUERY` | `epidemic::gameplay::processes::ProcessRecipeId` | `[[nodiscard]] constexpr bool operator==(const ProcessRecipeId&)const noexcept=default;`
- `e72ae9087dca8283` | `QUERY` | `epidemic::gameplay::processes::ProcessStepId` | `[[nodiscard]] constexpr bool operator==(const ProcessStepId&)const noexcept=default;`
- `e79779a43d84d530` | `QUERY` | `epidemic::gameplay::processes::ProcessOutputTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `ef7dbf44c425466b` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] std::vector<ProcessInstance> FindProcessesByStation(GameplayObjectRef station)const;`
- `f08dc1b8afc34d28` | `QUERY` | `epidemic::gameplay::processes::ProcessInstanceId` | `[[nodiscard]] constexpr bool operator==(const ProcessInstanceId&)const noexcept=default;`
- `f13fcdc548441a8d` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] std::vector<ProcessInstance> FindProcessesByActor(GameplayObjectRef actor)const;`
- `f32b42bce6d145f4` | `MUTATOR` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] foundation::Result<ProcessBatchCompletionReport> CompleteDue(GameplayTimePoint now);`
- `f88403e5d002a19a` | `QUERY` | `epidemic::gameplay::processes::ProcessInstanceId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `f99073acf6038e39` | `QUERY` | `epidemic::gameplay::processes::ProcessQualityId` | `[[nodiscard]] constexpr auto operator<=>(const ProcessQualityId&)const noexcept=default;`
- `fa07c23c1bcff5f1` | `QUERY` | `epidemic::gameplay::processes::ProcessInputId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `fab18f6ef693fa99` | `QUERY` | `epidemic::gameplay::processes::IProcessQualityProvider` | `[[nodiscard]] virtual ProcessQualityResult Resolve(const ProcessRecipe&recipe,const StartProcessRequest&request)const=0;`
- `fc643aa171c0eae7` | `CONSTRUCTOR` | `epidemic::gameplay::processes::ProcessesService` | `ProcessesService();`
- `fe63a37811c23f93` | `QUERY` | `epidemic::gameplay::processes::ProcessesService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`

## Local-ready projection

All 37 Goal 4 local criteria have a block-local `PASS` decision with concrete contract/state/test evidence in `_goal4_handoff/B03/local_ready.json`. All 15 dossier fields are `REVIEWED`. Canonical `docs/freeze/**` regeneration remains the serial integrator step after all eight Bxx deltas are merged.
