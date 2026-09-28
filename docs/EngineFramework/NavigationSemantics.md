# NavigationSemantics Goal 4 B05 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns semantic navigation profiles, layers and links/rules, their indexes, layer identity generator, revisions and the bounded navigation-semantics change journal.

The service remains the single authoritative owner of that state. Derived indexes, journals and diagnostic/read models are not independent semantic owners.

## Authoritative state and indexes

Admission/generated B05 inventory: 69 public callables, 60 mutation obligations, 1 lifecycle candidates, 14 stale-identity candidates and 0 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Multi-container mutations must complete fallible staging before authoritative publication. Deterministic query/order semantics must agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B05 coverage projection. Revision, identity-generator and journal publication is part of the mutation contract. Allocation/publication failure may not expose partially advanced primary state, index state, generator, revision or journal.

B05 allocation evidence uses source-private module seams rather than the shared process-global allocator override. The seams are implementation/test details and do not expand the public API.

## Lifecycle and identity

All emitted lifecycle and stale-identity candidates were reviewed. Invalid, removed, duplicate and stale identities are rejected or handled as the documented no-op without false publication. Retry does not silently resurrect terminal state.

## Persistence

Snapshot/restore is reviewed as candidate-state publication: validate first, build off-state, preserve the current service on failure, then atomically publish the candidate. Successful restore preserves generator/revision/cursor continuity required by the module snapshot contract.

## External boundaries

Capability lookup/evaluation is a semantic provider boundary. Failure is not authoritative navigation state and may not leave a partial layer/link/index publication.

## Threading contract

No additional internal synchronization guarantee is introduced by B05. These gameplay owners are treated as externally serialized/owner-thread services for Goal 4 local correctness. Engine-wide concurrency qualification remains Goal 6.

## Regression evidence

Registered module target: `EpidemicGameFrameworkNavigationSemanticsTests` from `EngineFramework/DevelopmentInfrastructure/Tests/navigation_semantics_tests.cpp`.

Goal 4 migrates restore failure evidence to narrow source-private fault points for profiles, layers, links, journal and indexes. Existing deterministic rule, invalid-reference, revision/cursor and snapshot regressions remain green.

## Goal 4 functional checklist

- [x] Semantic area/layer/rule registration.
- [x] Registry freeze.
- [x] Capability provider failure.
- [x] Route/query semantic constraints.
- [x] Layer create/update/remove.
- [x] Invalid world references.
- [x] Deterministic rule resolution.
- [x] Snapshot/restore semantic graph/indexes.

## Exact public API anchors

Each row is the exact reviewed contract for one B05 callable. The matching assertion is recorded in `_goal4_handoff/B05/public_api_anchors.json`.

- `024229478713755e` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] NavigationChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `075e65e889ae647a` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(NavigationSnapshot snapshot);`
- `0763bf79debae700` | `FACTORY` | `epidemic::gameplay::navigation_semantics::NavigationDomainId` | `static constexpr NavigationDomainId FromString(std::string_view s)noexcept`
- `0f4389269424c2c5` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> SetProfile(NavigationSemanticProfile profile,GameplayContext context={});`
- `0f656f135e198e03` | `LIFECYCLE` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `void Freeze()noexcept`
- `11f912822725d467` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLayerId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `16b860fce5699282` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] NavigationSnapshot CaptureSnapshot()const;`
- `1c897a994c08ae86` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationRuleId` | `[[nodiscard]] constexpr auto operator<=>(const NavigationRuleId&)const noexcept=default;`
- `1fb1d6a6bb9ebd7e` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] std::uint64_t RemoveLayersBySource(GameplayObjectRef source,GameplayContext context={});`
- `2233a99b1ab75a53` | `QUERY` | `epidemic::gameplay::navigation_semantics::TraversalModeSemanticId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalModeSemanticId&)const noexcept=default;`
- `22813cc630e1e217` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationReasonId` | `[[nodiscard]] constexpr bool operator==(const NavigationReasonId&)const noexcept=default;`
- `24e9e30f12c0215d` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationRuleId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `33971982125faa64` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> RemoveLayer(NavigationLayerId id,GameplayContext context={});`
- `33aa4b4bb3ca71e7` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLayerId` | `[[nodiscard]] constexpr bool operator==(const NavigationLayerId&)const noexcept=default;`
- `3c01c9d2a64d74bd` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> AddOrUpdateLink(NavigationSemanticLink link,GameplayContext context={});`
- `3faf74cc6f4aac05` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLayerTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `424fcf0e2cfb3fcc` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLayerId` | `[[nodiscard]] constexpr auto operator<=>(const NavigationLayerId&)const noexcept=default;`
- `434ad18be4a92d16` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> RemoveLink(NavigationLinkId id,GameplayContext context={});`
- `4593affdb37262f6` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationReasonId` | `[[nodiscard]] constexpr auto operator<=>(const NavigationReasonId&)const noexcept=default;`
- `473ab1167a1dc830` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationDomainId` | `[[nodiscard]] constexpr bool operator==(const NavigationDomainId&)const noexcept=default;`
- `4d142728a3fe3823` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] NavigationPermissionResult CanEnterArea(GameplayObjectRef subject,GameplayObjectRef area,TraversalModeSemanticId traversal_mode,const GameplayContext&context)const;`
- `4f834eef2855dfbc` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLinkTypeId` | `[[nodiscard]] constexpr auto operator<=>(const NavigationLinkTypeId&)const noexcept=default;`
- `560f7282e5767584` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] const NavigationSemanticLayer*FindLayer(NavigationLayerId id)const noexcept;`
- `57c522186fc0c90a` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLinkTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `6057ddb61babbc08` | `FACTORY` | `epidemic::gameplay::navigation_semantics::NavigationRuleId` | `static constexpr NavigationRuleId FromString(std::string_view s)noexcept`
- `645627267c386487` | `FACTORY` | `epidemic::gameplay::navigation_semantics::NavigationLayerId` | `static constexpr NavigationLayerId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `67d664cd301e63b9` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLayerTypeId` | `[[nodiscard]] constexpr auto operator<=>(const NavigationLayerTypeId&)const noexcept=default;`
- `743c354f5a6bc378` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] NavigationPermissionResult CanUseLink(GameplayObjectRef subject,NavigationLinkId link,TraversalModeSemanticId traversal_mode,const GameplayContext&context)const;`
- `74df3a93a1eb68e3` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> UpdateLayer(NavigationLayerId id,NavigationSemanticLayer replacement,Revision expected_revision,GameplayContext context={});`
- `757393ffbc6c20fd` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLinkId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `780709d8bb86e107` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationDomainId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `7bd39cda471ea3c7` | `QUERY` | `epidemic::gameplay::navigation_semantics::TraversalModeSemanticId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `81db724505ed77d9` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLinkId` | `[[nodiscard]] constexpr auto operator<=>(const NavigationLinkId&)const noexcept=default;`
- `849bb6b987c91746` | `CONSTRUCTOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `NavigationSemanticsService();`
- `86f8002716a3e64d` | `FACTORY` | `epidemic::gameplay::navigation_semantics::NavigationLayerId` | `static constexpr NavigationLayerId FromString(std::string_view s)noexcept`
- `889db909b3aa4e0c` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] const NavigationSemanticLink*FindLink(NavigationLinkId id)const noexcept;`
- `8c15cb2928bf4372` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] const NavigationSemanticProfile*FindProfile(GameplayObjectRef subject)const noexcept;`
- `8df8445f2e8fec4b` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLayerTypeId` | `[[nodiscard]] constexpr bool operator==(const NavigationLayerTypeId&)const noexcept=default;`
- `903e6181819a144b` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] bool IsFrozen()const noexcept`
- `92943b228211a9b5` | `DESTRUCTOR` | `epidemic::gameplay::navigation_semantics::INavigationCapabilityProvider` | `virtual ~INavigationCapabilityProvider()=default;`
- `a456ffdfae6be5ea` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] NavigationDiagnostics GetDiagnostics()const noexcept;`
- `ac07fd4d737e16a3` | `QUERY` | `epidemic::gameplay::navigation_semantics::INavigationFactProvider` | `[[nodiscard]] virtual bool HasFact(GameplayObjectRef subject,TypeId fact,const GameplayContext&context)const noexcept=0;`
- `b63795f55dd247ad` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLinkTypeId` | `[[nodiscard]] constexpr bool operator==(const NavigationLinkTypeId&)const noexcept=default;`
- `b8d9ae50d32f76a4` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] NavigationPermissionResult EvaluatePath(NavigationPermissionQuery query)const;`
- `ba5d1266477e89ba` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> SetLinkState(NavigationLinkId id,LinkState state,GameplayContext context={});`
- `bc06dfa5d72aadc1` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `bf0346413da62c95` | `FACTORY` | `epidemic::gameplay::navigation_semantics::NavigationReasonId` | `static constexpr NavigationReasonId FromString(std::string_view s)noexcept`
- `bf79336d41049548` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `c07ee8e0a1894ee1` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> RegisterDomain(NavigationDomainDefinition definition);`
- `c2260e647c06dbf3` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] std::vector<NavigationSemanticLink> FindLinksBetween(GameplayObjectRef from,GameplayObjectRef to)const;`
- `c302a4d3c78b5f28` | `QUERY` | `epidemic::gameplay::navigation_semantics::RefHash` | `[[nodiscard]] std::size_t operator()(GameplayObjectRef ref)const noexcept`
- `c3eb475928411231` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `void SetCapabilityProvider(const INavigationCapabilityProvider*provider)noexcept`
- `c7318c1798734ad1` | `DESTRUCTOR` | `epidemic::gameplay::navigation_semantics::INavigationFactProvider` | `virtual ~INavigationFactProvider()=default;`
- `c9e2617476278fe7` | `FACTORY` | `epidemic::gameplay::navigation_semantics::NavigationLayerTypeId` | `static constexpr NavigationLayerTypeId FromString(std::string_view s)noexcept`
- `cc27edb3bfbedfb7` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationReasonId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `cf84cbf18c885c39` | `FACTORY` | `epidemic::gameplay::navigation_semantics::NavigationLinkId` | `static constexpr NavigationLinkId FromString(std::string_view s)noexcept`
- `d77e0dfa2f94e0a9` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationRuleId` | `[[nodiscard]] constexpr bool operator==(const NavigationRuleId&)const noexcept=default;`
- `e7af29826f6cc2ad` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] std::uint64_t ExpireLayers(GameplayTimePoint now,GameplayContext context={});`
- `ea236b79fa9a055b` | `QUERY` | `epidemic::gameplay::navigation_semantics::INavigationCapabilityProvider` | `[[nodiscard]] virtual bool HasCapability(GameplayObjectRef subject,TypeId capability,Fixed min_parameter_micro)const noexcept=0;`
- `eb931e256bad53b7` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<NavigationLayerId> AddLayer(NavigationSemanticLayer layer,GameplayContext context={});`
- `ebc0489bbb04af30` | `FACTORY` | `epidemic::gameplay::navigation_semantics::NavigationLinkTypeId` | `static constexpr NavigationLinkTypeId FromString(std::string_view s)noexcept`
- `ed687eabe09c92b7` | `QUERY` | `epidemic::gameplay::navigation_semantics::TraversalModeSemanticId` | `[[nodiscard]] constexpr bool operator==(const TraversalModeSemanticId&)const noexcept=default;`
- `ee7e2b44da432795` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] foundation::Result<void> RegisterRule(NavigationRuleDefinition rule);`
- `ef65414de404afc0` | `MUTATOR` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `void SetFactProvider(const INavigationFactProvider*provider)noexcept`
- `f0f9f55953e438cb` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationDomainId` | `[[nodiscard]] constexpr auto operator<=>(const NavigationDomainId&)const noexcept=default;`
- `f99ada7281296f0e` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `fc13c7820e1dc0f5` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationSemanticsService` | `[[nodiscard]] std::vector<NavigationSemanticLayer> FindLayersInArea(GameplayObjectRef area)const;`
- `fe20cd34bfc3da13` | `FACTORY` | `epidemic::gameplay::navigation_semantics::TraversalModeSemanticId` | `static constexpr TraversalModeSemanticId FromString(std::string_view s)noexcept`
- `fed0b3c2537df2fa` | `QUERY` | `epidemic::gameplay::navigation_semantics::NavigationLinkId` | `[[nodiscard]] constexpr bool operator==(const NavigationLinkId&)const noexcept=default;`

## Local-ready projection

All 37 Goal 4 local criteria have a typed block-local `PASS` decision with exact contract/state/test evidence in `_goal4_handoff/B05/local_ready.json`. All 15 dossier fields are `REVIEWED`; canonical promotion remains the serial integrator step.
