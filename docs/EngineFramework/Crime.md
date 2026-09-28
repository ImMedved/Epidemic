# Crime Goal 4 local audit

## Scope

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Crime`. Admission: 100 public callables, 80 mutation obligations, 1 lifecycle candidates, 19 stale-identity candidates, 0 external-boundary candidates.

## Public contract review

- Law/jurisdiction/authority registration.
- Crime candidate evaluation and crime-record lifecycle.
- Evidence/witness duplicate and stale-reference behavior.
- Proof-state, bounty and law-response lifecycle.
- Revision/generator/journal boundary handling and multi-index consistency.
- Snapshot/restore validates all legal records before publication.

## Failure atomicity and boundary review

All revision-bearing mutations were reviewed for revision/generator exhaustion before authoritative mutation. Multi-container mutations were reviewed for rollback or staged publication. Snapshot restore builds/validates candidate state before live-state replacement. Allocation-failure evidence for this block no longer depends on the process-global Framework allocator helper; owned tests use module-local failure seams where allocation failure is evidence.

## Persistence and deterministic reads

The module snapshot surface is treated as local in-memory persistence evidence for Goal 4. Non-empty roundtrip, invalid restore, generator/revision continuity, journal epoch/cursor behavior and failed-restore pre-state preservation are covered by the module test executable. Whole-engine ordered restore and replay remain Goal 5.

## Dossier review

- Responsibility: REVIEWED.
- Dependency list: REVIEWED.
- Public headers and types: REVIEWED.
- Public mutation API: REVIEWED.
- Read/query API for invariants: REVIEWED.
- Authoritative state: REVIEWED.
- Derived/cache/index state: REVIEWED.
- ID spaces, generations, revisions and cursors: REVIEWED.
- State machines: REVIEWED.
- Local invariants: REVIEWED.
- Persistent and transient state: REVIEWED.
- Snapshot/restore contract: REVIEWED.
- External ports/callbacks/providers/backends: REVIEWED.
- Hard limits, budgets and complexity bounds: REVIEWED.
- Threading contract: REVIEWED.

## Test evidence

Primary regression source: `EngineFramework/DevelopmentInfrastructure/Tests/crime_tests.cpp`.
Registered target: `EpidemicGameFrameworkCrimeTests`.
Local Linux verification in the worker environment used direct C++23 compilation with `-Wall -Wextra -Wpedantic -Werror` and executed the module test binary. The repository top-level CMake configure is Windows-only and therefore cannot be used in this Linux worker environment.

## Goal 4 result

Block-local review result: LOCAL_READY candidate for serial integration. This document does not claim whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN`.

## Exact public API anchors

Each row is the exact reviewed contract for one B04 callable. The matching assertion is recorded in `_goal4_handoff/B04/public_api_anchors.json`.

- `08c9ada691f8407d` | `QUERY` | `epidemic::gameplay::crime::LawId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `0ab257e136234bc0` | `FACTORY` | `epidemic::gameplay::crime::LawResponseId` | `static constexpr LawResponseId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `0afad1c5414ff72b` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> ResolveBounty(BountyRecordId id,BountyState state,GameplayContext context={});`
- `14e5b915d440c7d8` | `QUERY` | `epidemic::gameplay::crime::CrimeRecordId` | `[[nodiscard]] constexpr auto operator<=>(const CrimeRecordId&)const noexcept=default;`
- `15f2358ee1504df9` | `FACTORY` | `epidemic::gameplay::crime::EvidenceId` | `static constexpr EvidenceId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `16252bb51df7f79c` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> RegisterLaw(LawDefinition law);`
- `16aef73d2f69c855` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::crime::BountyRecordId&v)const noexcept`
- `16fed7d6e09b8e45` | `QUERY` | `epidemic::gameplay::crime::BountyRecordId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `1877a44dfc082525` | `FACTORY` | `epidemic::gameplay::crime::AuthorityId` | `static constexpr AuthorityId FromString(std::string_view s)noexcept`
- `1adc61f1ce11f3a8` | `FACTORY` | `epidemic::gameplay::crime::WitnessRecordId` | `static constexpr WitnessRecordId FromString(std::string_view s)noexcept`
- `1cae57c10279262c` | `FACTORY` | `epidemic::gameplay::crime::CrimeRecordId` | `static constexpr CrimeRecordId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `1dd99d393f4484f7` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::crime::CrimeRecordId&v)const noexcept`
- `219cea0fea277d79` | `LIFECYCLE` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> FreezeDefinitions();`
- `238386cb942f6285` | `QUERY` | `epidemic::gameplay::crime::EvidenceId` | `[[nodiscard]] constexpr bool operator==(const EvidenceId&)const noexcept=default;`
- `242b66eb7a8d8c87` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::vector<EvidenceRecord> FindEvidence(CrimeRecordId crime)const;`
- `265e0029816c04b8` | `QUERY` | `epidemic::gameplay::crime::JurisdictionId` | `[[nodiscard]] constexpr auto operator<=>(const JurisdictionId&)const noexcept=default;`
- `29eddacd194b3cec` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] LegalityPreview PreviewLegality(GameplayObjectRef offender,CrimeTypeId type,GameplayObjectRef area)const;`
- `2aed6200b10687fa` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> RemoveWitness(WitnessRecordId witness,GameplayContext context={});`
- `2de025000f7e2502` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> ExpireCrime(CrimeRecordId id,GameplayContext context={});`
- `2fd669c138d9a6e1` | `QUERY` | `epidemic::gameplay::crime::EvidenceTypeId` | `[[nodiscard]] constexpr auto operator<=>(const EvidenceTypeId&)const noexcept=default;`
- `329c31cdefe68eb0` | `FACTORY` | `epidemic::gameplay::crime::BountyRecordId` | `static constexpr BountyRecordId FromString(std::string_view s)noexcept`
- `3566a78d8067a92a` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::crime::EvidenceId&v)const noexcept`
- `366f5e23c0d3b17e` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> RegisterLawResponseDefinition(LawResponseDefinition definition);`
- `391c875d8156a1d1` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] CrimeSnapshot CaptureSnapshot()const;`
- `3a64df0351f137f1` | `QUERY` | `epidemic::gameplay::crime::BountyRecordId` | `[[nodiscard]] constexpr bool operator==(const BountyRecordId&)const noexcept=default;`
- `3e17e07d2a9fa2ac` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::optional<BountyRecord> GetBounty(GameplayObjectRef offender,JurisdictionId jurisdiction)const;`
- `42dd40b8f6de07f6` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::vector<WitnessRecord> FindWitnesses(CrimeRecordId crime)const;`
- `44110d960a080c54` | `QUERY` | `epidemic::gameplay::crime::CrimeRecordId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4779196187e8e7f8` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::crime::WitnessRecordId&v)const noexcept`
- `489a3355e7ae2abb` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] const CrimeRecord*FindCrime(CrimeRecordId id)const noexcept;`
- `499ac53094eae617` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> RegisterJurisdiction(JurisdictionRecord jurisdiction);`
- `4b6f6b288ccc47a4` | `QUERY` | `epidemic::gameplay::crime::CrimeRecordId` | `[[nodiscard]] constexpr bool operator==(const CrimeRecordId&)const noexcept=default;`
- `4d43ff981ad7a915` | `FACTORY` | `epidemic::gameplay::crime::BountyRecordId` | `static constexpr BountyRecordId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `4e8e73654edf9332` | `FACTORY` | `epidemic::gameplay::crime::JurisdictionId` | `static constexpr JurisdictionId FromString(std::string_view s)noexcept`
- `4e9eee092d6b4448` | `QUERY` | `epidemic::gameplay::crime::JurisdictionId` | `[[nodiscard]] constexpr bool operator==(const JurisdictionId&)const noexcept=default;`
- `50e2355055babddb` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::size_t ExpireDue(GameplayTimePoint now,GameplayContext context={});`
- `536ecd98678d3272` | `QUERY` | `epidemic::gameplay::crime::BountyRecordId` | `[[nodiscard]] constexpr auto operator<=>(const BountyRecordId&)const noexcept=default;`
- `53d9b2cf54737c9c` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> ChangeProofState(CrimeRecordId id,CrimeProofState state,GameplayContext context={});`
- `555f0f1cabdc12f8` | `QUERY` | `epidemic::gameplay::crime::EvidenceTypeId` | `[[nodiscard]] constexpr bool operator==(const EvidenceTypeId&)const noexcept=default;`
- `5856ed5d92b6f20b` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::vector<CrimeRecord> FindCrimesByOffender(GameplayObjectRef offender)const;`
- `5a33a34dbbdf972b` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `void SetChangeJournalCapacity(std::size_t capacity)noexcept;`
- `5baec63adf56e301` | `QUERY` | `epidemic::gameplay::crime::WitnessRecordId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `5bfb1d3b586262bb` | `FACTORY` | `epidemic::gameplay::crime::LawResponseTypeId` | `static constexpr LawResponseTypeId FromString(std::string_view s)noexcept`
- `5d79d95f9e384cb5` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] bool DefinitionsFrozen()const noexcept`
- `5f8ddabc53f4f97b` | `QUERY` | `epidemic::gameplay::crime::LawResponseId` | `[[nodiscard]] constexpr bool operator==(const LawResponseId&)const noexcept=default;`
- `644cb9c0800a7e15` | `FACTORY` | `epidemic::gameplay::crime::LawResponseId` | `static constexpr LawResponseId FromString(std::string_view s)noexcept`
- `64c3b1a9372b9e51` | `QUERY` | `epidemic::gameplay::crime::CrimeTypeId` | `[[nodiscard]] constexpr auto operator<=>(const CrimeTypeId&)const noexcept=default;`
- `69306db3a720410c` | `FACTORY` | `epidemic::gameplay::crime::WitnessRecordId` | `static constexpr WitnessRecordId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `6de2d93f51b96c48` | `QUERY` | `epidemic::gameplay::crime::LawResponseTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `6df44f0956d201a1` | `QUERY` | `epidemic::gameplay::crime::LawId` | `[[nodiscard]] constexpr bool operator==(const LawId&)const noexcept=default;`
- `6df610a4fe904009` | `FACTORY` | `epidemic::gameplay::crime::CrimeRecordId` | `static constexpr CrimeRecordId FromString(std::string_view s)noexcept`
- `6e048b28c8a744b0` | `FACTORY` | `epidemic::gameplay::crime::WitnessTypeId` | `static constexpr WitnessTypeId FromString(std::string_view s)noexcept`
- `6ed40e370a00b322` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<AuthorityId> RegisterAuthority(AuthorityRecord authority);`
- `76db2c4ca4a12dba` | `FACTORY` | `epidemic::gameplay::crime::JurisdictionId` | `static constexpr JurisdictionId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `80cc15af0115a913` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] CrimeChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `814a2edaa6861118` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> RemoveEvidence(EvidenceId evidence,GameplayContext context={});`
- `843852b0efb9b9ae` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<WitnessRecordId> AddWitness(WitnessRecord witness,GameplayContext context={});`
- `8afd32723cbd2fe1` | `FACTORY` | `epidemic::gameplay::crime::EvidenceTypeId` | `static constexpr EvidenceTypeId FromString(std::string_view s)noexcept`
- `8c491e02cd5a8c18` | `QUERY` | `epidemic::gameplay::crime::WitnessTypeId` | `[[nodiscard]] constexpr bool operator==(const WitnessTypeId&)const noexcept=default;`
- `8e82ba201ea8c106` | `QUERY` | `epidemic::gameplay::crime::LawResponseTypeId` | `[[nodiscard]] constexpr bool operator==(const LawResponseTypeId&)const noexcept=default;`
- `93e7638ad88c7198` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] CrimeDiagnostics GetDiagnostics()const noexcept;`
- `95295ae1e1b2dee0` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::crime::AuthorityId&v)const noexcept`
- `9ce36d4bfd4ecfa5` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::vector<CrimeRecord> FindCrimesByJurisdiction(JurisdictionId jurisdiction)const;`
- `9eef49ec6b25fd39` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::vector<CrimeRecord> FindOpenCases()const;`
- `9f0d87725a1746c1` | `QUERY` | `epidemic::gameplay::crime::CrimeTypeId` | `[[nodiscard]] constexpr bool operator==(const CrimeTypeId&)const noexcept=default;`
- `a1b4c05a3dfc33c7` | `QUERY` | `epidemic::gameplay::crime::WitnessRecordId` | `[[nodiscard]] constexpr bool operator==(const WitnessRecordId&)const noexcept=default;`
- `a42f9d36415fd8ba` | `QUERY` | `epidemic::gameplay::crime::WitnessRecordId` | `[[nodiscard]] constexpr auto operator<=>(const WitnessRecordId&)const noexcept=default;`
- `a7412e1c34035364` | `QUERY` | `epidemic::gameplay::crime::AuthorityId` | `[[nodiscard]] constexpr auto operator<=>(const AuthorityId&)const noexcept=default;`
- `a7aa82d423f06e70` | `QUERY` | `epidemic::gameplay::crime::WitnessTypeId` | `[[nodiscard]] constexpr auto operator<=>(const WitnessTypeId&)const noexcept=default;`
- `a819297f531983c8` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::crime::LawResponseId&v)const noexcept`
- `aa954205e8ef9cb7` | `QUERY` | `epidemic::gameplay::crime::LawResponseTypeId` | `[[nodiscard]] constexpr auto operator<=>(const LawResponseTypeId&)const noexcept=default;`
- `acf08149d82a94de` | `QUERY` | `epidemic::gameplay::crime::AuthorityId` | `[[nodiscard]] constexpr bool operator==(const AuthorityId&)const noexcept=default;`
- `ade1b26df41eade1` | `QUERY` | `epidemic::gameplay::crime::WitnessTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `ae4e8538a767d99e` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `af45bf07f8c6eb42` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::optional<JurisdictionRecord> GetApplicableJurisdiction(GameplayObjectRef area)const;`
- `b04f9d28f3a9c682` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> ChangeCaseState(CrimeRecordId id,CrimeCaseState state,GameplayContext context={});`
- `b290fb3d485f177c` | `FACTORY` | `epidemic::gameplay::crime::EvidenceId` | `static constexpr EvidenceId FromString(std::string_view s)noexcept`
- `bb55a7bc5459806f` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `ca784c1df4b294e6` | `FACTORY` | `epidemic::gameplay::crime::LawId` | `static constexpr LawId FromString(std::string_view s)noexcept`
- `cbbf361934c392de` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::crime::JurisdictionId&v)const noexcept`
- `ce249f980b89fd30` | `QUERY` | `epidemic::gameplay::crime::EvidenceId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d11eb305901ac698` | `FACTORY` | `epidemic::gameplay::crime::CrimeTypeId` | `static constexpr CrimeTypeId FromString(std::string_view s)noexcept`
- `d1a1977d2b5f226f` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(CrimeSnapshot snapshot);`
- `d705ff8c11567132` | `QUERY` | `epidemic::gameplay::crime::CrimeTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d7d5c27d80833b86` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `d7d7c762007f2630` | `QUERY` | `epidemic::gameplay::crime::LawResponseId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d8e6615dd7f41b60` | `QUERY` | `epidemic::gameplay::crime::LawResponseId` | `[[nodiscard]] constexpr auto operator<=>(const LawResponseId&)const noexcept=default;`
- `dad7dbfd558a26cd` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<BountyRecordId> CreateBounty(BountyRecord bounty);`
- `df213c91a2ed5db5` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<LawResponseId> GenerateLawResponse(LawResponseRequest request);`
- `e0f0421da91421d9` | `QUERY` | `epidemic::gameplay::crime::AuthorityId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e320581b771e7af8` | `QUERY` | `epidemic::gameplay::crime::JurisdictionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e373eb591d412966` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<CrimeEvaluationResult> EvaluateCrimeCandidate(CrimeCandidate candidate,CrimeCandidatePolicy policy=CrimeCandidatePolicy::RecordAlways);`
- `e38ac087fa15d0ed` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `void PruneChangesThrough(std::uint64_t sequence);`
- `e96a0cfab667f90e` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<EvidenceId> AddEvidence(EvidenceRecord evidence,GameplayContext context={});`
- `eb8950e7e6bb1450` | `QUERY` | `epidemic::gameplay::crime::EvidenceTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `eb95c846966fd6f0` | `QUERY` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] std::vector<CrimeRecord> FindCrimesByVictim(GameplayObjectRef victim)const;`
- `f10d1d57598bba2b` | `FACTORY` | `epidemic::gameplay::crime::AuthorityId` | `static constexpr AuthorityId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `f130310ad212d8f3` | `MUTATOR` | `epidemic::gameplay::crime::CrimeService` | `[[nodiscard]] foundation::Result<void> PruneTerminalCrime(CrimeRecordId id,GameplayContext context={});`
- `f64fe8283452ac51` | `QUERY` | `epidemic::gameplay::crime::LawId` | `[[nodiscard]] constexpr auto operator<=>(const LawId&)const noexcept=default;`
- `f95a8612a0d8fbc2` | `QUERY` | `epidemic::gameplay::crime::EvidenceId` | `[[nodiscard]] constexpr auto operator<=>(const EvidenceId&)const noexcept=default;`
