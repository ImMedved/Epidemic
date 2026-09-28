# Traversal Goal 4 local audit

Status: B06 worker review complete. This is block-local Goal 4 evidence and not a whole-Framework freeze claim.

## Responsibility

Owns traversal modes/profiles, subject traversal state, capability grants, routes, sessions, carrier bindings, generators, revision and change journal.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Traversal/include/Epidemic/GameFramework/Traversal/traversal.h`. B06 does not change the public header.

## Dependency list

Direct production links are GameFramework Foundation and EngineBase Foundation only.

## External ports/callbacks/providers/backends

Materialization capability is queried through a borrowed port. Runtime/navigation ownership remains outside this module.

## Authoritative state

Modes/profiles, subject states, capability grants, routes, live sessions, carrier bindings, ID generators, revision and bounded journal.

## Derived/cache/index state

Carrier passenger queries and mode/capability availability are derived from authoritative maps plus immutable definitions.

## ID spaces, generations, revisions and cursors

Session, route and capability-grant IDs use monotonic generators. Every state-changing family now checks revision exhaustion before authoritative mutation (`G4-TRAV-001`).

## State machines

Traversal session covers active/suspended and terminal completion/cancel/failure. Carrier board/disembark and route/capability lifetime are explicit semantic lifecycles.

## Persistent and transient state

Snapshot persists traversal states, live persistent routes/grants/sessions/bindings, generators, revision and journal continuity. Transient/session-only routes are not reusable persistent identity.

## Snapshot/restore contract

Restore validates profile/mode/reference consistency, live session cross-links, carrier bindings, generator positions and journal ordering before swapping candidate state. Private candidate/pre-commit seams prove failure atomicity.

## Threading contract

Owner-thread/external-serialization gameplay state.

## Public mutation API

Capability, profile/state, mode, route, session and carrier mutations preflight revision before destructive state changes. Fallible grant/profile/route/session/binding publication is staged before committing revision/generator/cross-links. This closes `G4-TRAV-001/002`.

## Read/query API for invariants

State/session/route/capability/carrier queries witness live cross-links, mode validity and deterministic ordering.

## Local invariants

`TraversalState::active_session` agrees with a live session; route/session subject and mode agree; one passenger has at most one carrier binding; failure before publication preserves revision/generators/cross-links. Journal allocation failure rotates epoch without invalidating accepted state.

## Hard limits, budgets and complexity bounds

Revision and ID generator exhaustion are explicit. Journal sequence exhaustion remains the documented snapshot-required terminal mode and is not conflated with revision exhaustion.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/traversal_tests.cpp` covers capabilities, mode changes, routes, sessions, carrier bindings, persistence and journal exhaustion. Goal 4 adds max-revision regressions for all named mutation families plus deterministic publication/restore/journal fault seams. Target: `EpidemicGameFrameworkTraversalTests`.

## Exact public API anchors

Each row is the exact reviewed contract for one B06 callable. The matching assertion is recorded in `_goal4_handoff/B06/public_api_anchors.json`.

- `03d324ae2fd245c1` | `QUERY` | `epidemic::gameplay::traversal::TraversalProfileId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalProfileId&)const noexcept=default;`
- `0d05a121c9ea7b12` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] TraversalDiagnostics GetDiagnostics()const noexcept;`
- `158320d889471434` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> RegisterProfile(TraversalProfile profile);`
- `1a1c4b9af3ce92a7` | `CONSTRUCTOR` | `epidemic::gameplay::traversal::TraversalCapabilityValue` | `constexpr TraversalCapabilityValue(TraversalCapabilityId capability,Fixed parameter=1'000'000)noexcept`
- `1f9ff65a5cc1343e` | `QUERY` | `epidemic::gameplay::traversal::TraversalSessionId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalSessionId&)const noexcept=default;`
- `228622360c3694bf` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> AssignProfile(GameplayObjectRef subject,TraversalProfileId profile,GameplayContext context={});`
- `2518b4d20ea5240c` | `FACTORY` | `epidemic::gameplay::traversal::TraversalCarrierRoleId` | `static constexpr TraversalCarrierRoleId FromString(std::string_view s)noexcept`
- `2aedb95e2e55d054` | `FACTORY` | `epidemic::gameplay::traversal::TraversalSessionId` | `static constexpr TraversalSessionId FromString(std::string_view s)noexcept`
- `2ff0dae498b0a23c` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> RegisterMode(TraversalModeDefinition definition);`
- `3128dcd87d842706` | `LIFECYCLE` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> ResumeSession(TraversalSessionId id,GameplayContext context={});`
- `364597057ad165d3` | `DESTRUCTOR` | `epidemic::gameplay::traversal::ITraversalMaterializationProvider` | `virtual ~ITraversalMaterializationProvider()=default;`
- `364620e5e6b6cb5d` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] const TraversalModeDefinition*FindMode(TraversalModeId id)const noexcept;`
- `3f5aeafb3a0d3e02` | `FACTORY` | `epidemic::gameplay::traversal::TraversalRouteId` | `static constexpr TraversalRouteId FromString(std::string_view s)noexcept`
- `4248a428d381cc73` | `QUERY` | `epidemic::gameplay::traversal::TraversalReasonId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalReasonId&)const noexcept=default;`
- `429789076f77e1c2` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> CompleteSession(TraversalSessionId id,GameplayContext context={});`
- `49c71413a7adaaec` | `QUERY` | `epidemic::gameplay::traversal::TraversalCapabilityGrantId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4ce4daa9557fd3ec` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] Fixed EffectiveCapabilityParameter(GameplayObjectRef subject,TraversalCapabilityId capability)const noexcept;`
- `4f19e514d5219635` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] std::vector<TraversalCarrierBinding> FindCarrierPassengers(GameplayObjectRef carrier)const;`
- `4f6aac005fb5b617` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] TraversalResult CanUseMode(GameplayObjectRef subject,TraversalModeId mode)const;`
- `506c9893da8c08f5` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<TraversalRouteId> RegisterRoute(TraversalRoute route);`
- `5355b61ee242f9f5` | `LIFECYCLE` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<TraversalSessionId> StartSession(GameplayObjectRef subject,TraversalModeId mode,TraversalRouteId route={},GameplayTimePoint started_at={},GameplayContext context={});`
- `5515105b06537453` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(TraversalSnapshot snapshot);`
- `58f4f7bf4f7941ad` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> SuspendSession(TraversalSessionId id,GameplayContext context={});`
- `5985ede592ca5df3` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] const TraversalState*FindState(GameplayObjectRef subject)const noexcept;`
- `59d62b670e99770c` | `FACTORY` | `epidemic::gameplay::traversal::TraversalReasonId` | `static constexpr TraversalReasonId FromString(std::string_view s)noexcept`
- `5d014d8fdaca5aea` | `QUERY` | `epidemic::gameplay::traversal::TraversalCapabilityValue` | `[[nodiscard]] constexpr bool operator==(const TraversalCapabilityValue&)const noexcept=default;`
- `5d78510f35197e0f` | `QUERY` | `epidemic::gameplay::traversal::TraversalCarrierRoleId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalCarrierRoleId&)const noexcept=default;`
- `6083f616e2532670` | `QUERY` | `epidemic::gameplay::traversal::TraversalCapabilityGrantId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalCapabilityGrantId&)const noexcept=default;`
- `667d0b9c5b5a8ea0` | `QUERY` | `epidemic::gameplay::traversal::TraversalRouteId` | `[[nodiscard]] constexpr bool operator==(const TraversalRouteId&)const noexcept=default;`
- `6c28c638e78c78e6` | `LIFECYCLE` | `epidemic::gameplay::traversal::TraversalService` | `void Freeze()noexcept`
- `6eff0b582abf10b5` | `CONSTRUCTOR` | `epidemic::gameplay::traversal::TraversalService` | `TraversalService();`
- `6f733c7689941f2c` | `QUERY` | `epidemic::gameplay::traversal::ITraversalMaterializationProvider` | `[[nodiscard]] virtual bool HasRuntimeProjection(GameplayObjectRef subject)const noexcept=0;`
- `6fc31406d2d93a68` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> RemoveRoute(TraversalRouteId route,GameplayContext context={});`
- `702d4b02b7888c26` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<TraversalCapabilityGrantId> GrantCapability(TraversalCapabilityGrant grant,GameplayContext context={});`
- `7162a0cdf334eea7` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] std::uint64_t RevokeCapabilitiesBySource(GameplayObjectRef subject,GameplayObjectRef source,GameplayContext context={});`
- `770042e9cee41dda` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] const TraversalProfile*FindProfile(TraversalProfileId id)const noexcept;`
- `7876939b2667ebd7` | `QUERY` | `epidemic::gameplay::traversal::TraversalCapabilityId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `7b2397bb2ae5d2c3` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> RevokeCapability(TraversalCapabilityGrantId grant,GameplayContext context={});`
- `7b7dc572345181d3` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `7d4feaa0fc088dae` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<TraversalResult> ChangeMode(ChangeTraversalModeRequest request);`
- `7e42b661d0518609` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> FailSession(TraversalSessionId id,TraversalReasonId reason={},GameplayContext context={});`
- `8e833763ada4609d` | `QUERY` | `epidemic::gameplay::traversal::TraversalReasonId` | `[[nodiscard]] constexpr bool operator==(const TraversalReasonId&)const noexcept=default;`
- `924a6416376d5534` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] const TraversalRoute*FindRoute(TraversalRouteId id)const noexcept;`
- `937a4d353e685852` | `FACTORY` | `epidemic::gameplay::traversal::TraversalSessionId` | `static constexpr TraversalSessionId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `9b3370ecb1d50644` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> BoardCarrier(GameplayObjectRef passenger,GameplayObjectRef carrier,TraversalModeId mode,TraversalCarrierRoleId role,GameplayContext context={});`
- `a508aaf12adadae5` | `QUERY` | `epidemic::gameplay::traversal::TraversalSessionId` | `[[nodiscard]] constexpr bool operator==(const TraversalSessionId&)const noexcept=default;`
- `a51997b053f9504b` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> RemoveState(GameplayObjectRef subject,GameplayContext context={});`
- `aaaf96a6bf4903d9` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> Disembark(GameplayObjectRef passenger,GameplayContext context={});`
- `abcf06bc42e60ff7` | `QUERY` | `epidemic::gameplay::traversal::TraversalModeId` | `[[nodiscard]] constexpr bool operator==(const TraversalModeId&)const noexcept=default;`
- `b0d815d95e56e86a` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] std::uint64_t RemoveRoutesBySource(GameplayObjectRef source,GameplayContext context={});`
- `b2123730b27ea653` | `QUERY` | `epidemic::gameplay::traversal::ITraversalMaterializationProvider` | `[[nodiscard]] virtual bool IsMaterialized(GameplayObjectRef subject)const noexcept=0;`
- `b247f8e0484068a0` | `QUERY` | `epidemic::gameplay::traversal::TraversalCarrierRoleId` | `[[nodiscard]] constexpr bool operator==(const TraversalCarrierRoleId&)const noexcept=default;`
- `b38b6207d230cd6c` | `QUERY` | `epidemic::gameplay::traversal::TraversalRouteId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `b6abc187a99fd2c1` | `QUERY` | `epidemic::gameplay::traversal::TraversalCapabilityValue` | `[[nodiscard]] constexpr auto operator<=>(const TraversalCapabilityValue&)const noexcept=default;`
- `b824e7a87b4b77ec` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] bool IsFrozen()const noexcept`
- `b99d62bf200485a5` | `QUERY` | `epidemic::gameplay::traversal::TraversalProfileId` | `[[nodiscard]] constexpr bool operator==(const TraversalProfileId&)const noexcept=default;`
- `bba747795f83964c` | `QUERY` | `epidemic::gameplay::traversal::TraversalModeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `bd2163f103c822ba` | `QUERY` | `epidemic::gameplay::traversal::TraversalCarrierRoleId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `bec4ebdb0cdfa7f5` | `QUERY` | `epidemic::gameplay::traversal::TraversalProfileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `bf1caaf25499a0ba` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `c06b06c1dff6143d` | `FACTORY` | `epidemic::gameplay::traversal::TraversalCapabilityId` | `static constexpr TraversalCapabilityId FromString(std::string_view s)noexcept`
- `c25b51234ddfa3b3` | `QUERY` | `epidemic::gameplay::traversal::RefHash` | `[[nodiscard]] std::size_t operator()(GameplayObjectRef ref)const noexcept`
- `c38d2461d99bddac` | `QUERY` | `epidemic::gameplay::traversal::TraversalRouteId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalRouteId&)const noexcept=default;`
- `c52d5b12fe7eaa21` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] std::vector<TraversalState> FindSubjectsUsingMode(TraversalModeId mode)const;`
- `c5b9dee93e020620` | `QUERY` | `epidemic::gameplay::traversal::TraversalReasonId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `c82b76aa46cb415a` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `cd679f45d7947fa4` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] TraversalChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `d21356bb83ce1cc8` | `QUERY` | `epidemic::gameplay::traversal::TraversalCapabilityId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalCapabilityId&)const noexcept=default;`
- `d7fb0c2ade63e02e` | `CONSTRUCTOR` | `epidemic::gameplay::traversal::TraversalCapabilityValue` | `constexpr TraversalCapabilityValue()noexcept=default;`
- `e04b451a20841050` | `FACTORY` | `epidemic::gameplay::traversal::TraversalModeId` | `static constexpr TraversalModeId FromString(std::string_view s)noexcept`
- `e146c5f993d574b7` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] bool HasCapability(GameplayObjectRef subject,TraversalCapabilityId capability,Fixed min_parameter_micro=0)const noexcept;`
- `e2033a065e35881f` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] const TraversalSession*FindSession(TraversalSessionId id)const noexcept;`
- `e2862f25f8ca4d35` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `void SetMaterializationProvider(const ITraversalMaterializationProvider*provider)noexcept`
- `e3345e96a6a0dc53` | `QUERY` | `epidemic::gameplay::traversal::TraversalSessionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e6b99eb0a5fad910` | `QUERY` | `epidemic::gameplay::traversal::WorldPosition` | `[[nodiscard]] constexpr bool operator==(const WorldPosition&)const noexcept=default;`
- `eb181708d9b73bfc` | `FACTORY` | `epidemic::gameplay::traversal::TraversalProfileId` | `static constexpr TraversalProfileId FromString(std::string_view s)noexcept`
- `f0d72434025043b7` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] foundation::Result<void> CancelSession(TraversalSessionId id,TraversalReasonId reason={},GameplayContext context={});`
- `f23aa43c450db5c1` | `MUTATOR` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] std::uint64_t ExpireCapabilities(GameplayTimePoint now,GameplayContext context={});`
- `f29ee4413de11819` | `QUERY` | `epidemic::gameplay::traversal::TraversalModeId` | `[[nodiscard]] constexpr auto operator<=>(const TraversalModeId&)const noexcept=default;`
- `f5c2d1777b4d98a3` | `QUERY` | `epidemic::gameplay::traversal::TraversalCapabilityId` | `[[nodiscard]] constexpr bool operator==(const TraversalCapabilityId&)const noexcept=default;`
- `f904e40b8b8161de` | `QUERY` | `epidemic::gameplay::traversal::TraversalService` | `[[nodiscard]] TraversalSnapshot CaptureSnapshot()const;`
- `fb42058b2f702f97` | `QUERY` | `epidemic::gameplay::traversal::TraversalCapabilityValue` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
