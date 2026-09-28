# Entities Goal 4 local audit

Status: B02 worker review complete. This document is module-local handoff evidence for Goal 4. It does not by itself promote the module or the whole Framework to canonical `LOCAL_READY`/`FROZEN`; serial integration and the official Windows/MSVC gates remain authoritative.

## Responsibility

Owns gameplay entity archetype registration, entity records, lifecycle, part references, materialization state and entity change journal. It is the only owner of entity slot/generation bookkeeping in this module.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Entities/include/Epidemic/GameFramework/Entities/entities.h`. The admission inventory in `docs/freeze/coverage_manifests.json` is the exact callable review input; B02 does not change public headers or public signatures.

## Dependency list

Direct production links are `EpidemicGameFrameworkFoundation` and `EpidemicFoundation`. No peer GameplayWorldStateOwner or Runtime module dependency is added by this delta.

## External ports/callbacks/providers/backends

No external backend. Public inputs are gameplay value types and GameplayTagRegistry query support; no callback is retained.

## Authoritative state

Archetype registry; slot vector; free-slot list; EntityId to slot index; pending-destruction IDs/reasons/contexts; entity ID generator; revision; bounded change journal.

## Derived/cache/index state

id_to_slot_ and free_slots_ derive from slots_; pending destruction maps describe pending lifecycle work and are rebuilt/validated on restore.

## ID spaces, generations, revisions and cursors

EntityId uses the service-owned MonotonicIdGenerator. EntityHandle carries slot plus generation and stale handles do not resolve after slot reuse. Revision and ChangeCursor epoch/sequence are monotonic and checked at boundaries.

## State machines

Registry is mutable until Freeze. Entity lifecycle covers Creating, Alive, Dormant, PendingDestroy, Destroyed and Removed; materialization has a separate state machine. Destroy requests are staged, terminal destruction is committed, and physical removal is explicit.

## Persistent and transient state

Registry/build-definition state is not serialized as reusable runtime identity. Snapshot carriers persist only the state described below; provider/backend pointers and derived indexes remain transient/rebuilt state.

## Snapshot/restore contract

EntitySnapshot persists records, pending destruction provenance, ID generator, revision and journal continuity. Restore validates IDs, archetypes, parts, revisions, pending references and generator position before replacing live state. Restore uses module-private `RestoreCandidateBuild` and `RestoreBeforeCommit` seams to prove that candidate-construction and publication-boundary failures preserve the previous live state.

## Threading contract

The current module is owner-thread/external-serialization gameplay state. No stronger thread-safety guarantee is introduced by Goal 4. Goal 6 remains responsible for engine-wide concurrency qualification.

## Public mutation API

Registration rejects duplicate/mismatched archetypes. Create supports deterministic requested IDs without allowing collisions. Activate/deactivate, materialization, convert, tags, destruction and remove validate stale/terminal state. No-op operations do not advance revision. Every admission mutator has success/no-op/precondition/failure decisions recorded in `_goal4_handoff/B02/coverage_reviews.json`.

## Read/query API for invariants

Read/query methods are used as invariant witnesses in `entities_tests.cpp` and are anchored per callable in `_goal4_handoff/B02/public_api_anchors.json`. Queries do not acquire hidden ownership of external objects.

## Local invariants

All live IDs map to exactly one valid slot; handle generation matches the slot generation; part references belong to the current entity/archetype; secondary lifecycle/tag/archetype queries derive from authoritative records; a failed restore/mutation does not partially publish revision or journal state.

## Hard limits, budgets and complexity bounds

ID/revision/change-sequence exhaustion is controlled. Journal retention is bounded by module policy. Entity queries return deterministic record ordering where the public contract exposes ordered vectors.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/entities_tests.cpp` exercises duplicate/freeze, create/convert, lifecycle, stale handles under slot reuse, requested-ID generator continuity, invalid snapshot, pending-destruction provenance, restore failure atomicity and high-cardinality creation. Target: `EpidemicGameFrameworkEntitiesTests`. Portable audit execution used C++23 GCC with `-Wall -Wextra -Wpedantic -Werror` in Debug-like (`-O0 -g`) and Release-like (`-O2 -DNDEBUG`) configurations. The official CMake/MSVC Debug/Release run is intentionally left to serial integration because this worker environment is non-Windows and the project Win32 Platform target rejects configuration here.

## Callable-specific closure contracts

Each row is the reviewed contract for one public callable; the ID is the stable inventory key used by the Goal 4 evidence ledger.

- `005bc73deb231062` — `epidemic::gameplay::entities::EntityPartId` / `[[nodiscard]] constexpr bool operator==(const EntityPartId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `02aa7ae9ee216fb3` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `03e0f4d28b7c6c62` — `epidemic::gameplay::entities::EntityArchetypeIdHash` / `[[nodiscard]] std::size_t operator()(EntityArchetypeId id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `081cd04929faeb60` — `epidemic::gameplay::entities::EntityArchetypeId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `09b5f7f795cb54df` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] EntitySnapshot CaptureSnapshot()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `0d7ccbcb1cc7fece` — `epidemic::gameplay::entities::EntityArchetypeId` / `[[nodiscard]] constexpr explicit operator bool()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `10359eea771af8dd` — `epidemic::gameplay::entities::EntityPartRef` / `[[nodiscard]] constexpr auto operator<=>(const EntityPartRef&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1a5649d7f46cb5ae` — `epidemic::gameplay::entities::EntityId` / `[[nodiscard]] constexpr auto operator<=>(const EntityId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1b9aa9115a9c3214` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> Deactivate(EntityId id,GameplayContext context={});`: performs the documented lifecycle transition and preserves state when that transition is not admissible.
- `1e9c8951df466210` — `epidemic::gameplay::entities::EntityService` / `void Freeze()noexcept`: performs the documented lifecycle transition and preserves state when that transition is not admissible.
- `20601373f9bae0d8` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] bool Exists(EntityId id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `228284a19159d33d` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] bool IsFrozen()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `28cf52ef2b472599` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> RestoreSnapshot(EntitySnapshot snapshot);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `2d3fa6b864671029` — `epidemic::gameplay::entities::EntityHandle` / `[[nodiscard]] constexpr explicit operator bool()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `2f80a1a06cc327fe` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::vector<EntityPartRef> GetParts(EntityId entity)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `30c0a471dd340ab2` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] Revision CurrentRevision()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `331decdb61f3bb27` — `std::hash` / `[[nodiscard]] size_t operator()(epidemic::gameplay::entities::EntityArchetypeId id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `3ac0157782fb21b3` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::vector<EntityRecord> AllEntities()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `3c051dc5eb9b016c` — `epidemic::gameplay::entities::EntityArchetypeId` / `[[nodiscard]] constexpr bool operator==(const EntityArchetypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `3e5c90af21ae55b1` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> RemoveTag(EntityId id,TagId tag,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `477c19f0af2021bd` — `std::hash` / `[[nodiscard]] size_t operator()(epidemic::gameplay::entities::EntityPartId id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `4b4d3c3097388b68` — `epidemic::gameplay::entities::EntityHandle` / `[[nodiscard]] constexpr bool operator==(const EntityHandle&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `4c14b697c71ac179` — `epidemic::gameplay::entities::EntityArchetypeId` / `[[nodiscard]] constexpr auto operator<=>(const EntityArchetypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `4e262e43d854c26d` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::vector<EntityRecord> FindByTag(TagId tag,const GameplayTagRegistry&registry)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `53d01a4189339e57` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> Convert(EntityId id,EntityArchetypeId new_archetype,EntityConversionPolicy policy={},GameplayContext context={});`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `579413603deb8a7a` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::vector<EntityRecord> FindByArchetype(EntityArchetypeId archetype)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `59170cecaf00744a` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] const EntityArchetypeDefinition*FindArchetype(EntityArchetypeId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5b19b8f1d2773e30` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::optional<EntityRecord> Find(EntityId id)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5c7bfba9c6a14daa` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] static constexpr GameplayObjectRef ToGameplayObjectRef(EntityId id)noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `60b289846fc16b14` — `epidemic::gameplay::entities::EntityPartRef` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `660d2340356bc52b` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] EntitiesDiagnostics GetDiagnostics()const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `6753912775c34fc1` — `epidemic::gameplay::entities::EntityId` / `[[nodiscard]] static constexpr EntityId FromString(std::string_view value)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `67d8b14c8baf6acc` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> RequestDestroy(EntityId id,EntityDestroyReason reason,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `69060e30bf54157c` — `epidemic::gameplay::entities::EntityPartId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `707f3ce02151c5be` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<CreateEntityResult> Create(CreateEntityRequest request);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `7354645bf758a939` — `epidemic::gameplay::entities::EntityService` / `void PruneChangesBefore(std::uint64_t sequence);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `73bc8ee81884c499` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> SetMaterializationState(EntityId id,EntityMaterializationState state,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `77c571c963966072` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] static constexpr EntityId FromGameplayObjectRef(GameplayObjectRef ref)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `7de6118799d73701` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] const EntityPartDefinition*FindPartDefinition(EntityArchetypeId archetype,EntityPartId part)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `82933059316c0069` — `epidemic::gameplay::entities::EntityPartId` / `[[nodiscard]] constexpr explicit operator bool()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `86a3a403ad66950f` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] static constexpr GameplayObjectPartRef ToGameplayObjectPartRef(EntityPartRef ref)noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `8e8d5b2a7b36e907` — `epidemic::gameplay::entities::EntityId` / `[[nodiscard]] static constexpr EntityId FromRaw(std::uint64_t high,std::uint64_t low)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `8f6eb63673cd2a81` — `epidemic::gameplay::entities::EntityService` / `EntityService();`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `90350fa4c4b7b326` — `epidemic::gameplay::entities::EntityIdHash` / `[[nodiscard]] std::size_t operator()(const EntityId&id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `95538220c887728a` — `epidemic::gameplay::entities::EntityHandle` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `99756d3c5fee7c77` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::optional<EntityRecord> Resolve(EntityHandle handle)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `9a4cbf0e698dbef0` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::vector<EntityRecord> FindByLifecycle(EntityLifecycleState lifecycle)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `9da26f346cb351f7` — `epidemic::gameplay::entities::EntityPartRef` / `[[nodiscard]] constexpr explicit operator bool()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a5c8d637299b0c87` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> Remove(EntityId id,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `a9499946d0e64a7a` — `epidemic::gameplay::entities::EntityPartIdHash` / `[[nodiscard]] std::size_t operator()(EntityPartId id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ae417549a8020012` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::optional<EntityMaterializationPolicy> MaterializationPolicyOf(EntityId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `b4428643b8c68f08` — `std::hash` / `[[nodiscard]] size_t operator()(const epidemic::gameplay::entities::EntityId&id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `be5f91fe2c05c68c` — `epidemic::gameplay::entities::EntityPartId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `c3838e7bc693ce3f` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] static constexpr EntityPartRef FromGameplayObjectPartRef(GameplayObjectPartRef ref)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `c75ed138fdf5cb66` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] EntityHandle HandleOf(EntityId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d4638102647e18e6` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d54d605e611c4f8b` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> Activate(EntityId id,GameplayContext context={});`: performs the documented lifecycle transition and preserves state when that transition is not admissible.
- `d5f0345d80724aef` — `epidemic::gameplay::entities::EntityId` / `[[nodiscard]] constexpr bool operator==(const EntityId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d647f398cf996ab4` — `epidemic::gameplay::entities::EntityPartId` / `[[nodiscard]] static constexpr EntityPartId FromString(std::string_view value)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `d9cb2bfdc707f452` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::optional<EntityPartRef> GetPart(EntityId entity,EntityPartId part)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `dcbe55e922c9a629` — `epidemic::gameplay::entities::EntityPartRef` / `[[nodiscard]] constexpr bool operator==(const EntityPartRef&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `dcc73ce81ff4ac7e` — `epidemic::gameplay::entities::EntityId` / `[[nodiscard]] constexpr std::uint64_t Low()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e05e89eba1275aa9` — `epidemic::gameplay::entities::EntityArchetypeId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e12cc5ada1218712` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] EntityChangeBatch ReadChangesSince(ChangeCursor cursor)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e2971501fcc23efb` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<std::vector<EntityRecord>> CommitPendingDestruction();`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `e542c571d7e04970` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] bool RequiresMaterialization(EntityId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e5a3c0cefd946b10` — `epidemic::gameplay::entities::EntityId` / `[[nodiscard]] constexpr std::uint64_t High()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ea63c0d11bbbfa30` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::vector<EntityRecord> FindByMaterialization(EntityMaterializationState state)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f2f463fdde95dbdb` — `epidemic::gameplay::entities::EntityId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f30633dfee18d6de` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<EntityArchetypeId> RegisterArchetype(EntityArchetypeDefinition definition);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `f367fefb84ae74da` — `epidemic::gameplay::entities::EntityId` / `[[nodiscard]] constexpr explicit operator bool()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f86bea83d7f5693e` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] foundation::Result<void> AddTag(EntityId id,TagId tag,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `f8e995c7a2bae8a2` — `epidemic::gameplay::entities::EntityService` / `[[nodiscard]] std::vector<EntityPartRef> GetPartAncestors(EntityPartRef part)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `fe9a8da5ae4aa136` — `epidemic::gameplay::entities::EntityPartId` / `[[nodiscard]] constexpr auto operator<=>(const EntityPartId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `fee2fceb98b7ed07` — `epidemic::gameplay::entities::EntityArchetypeId` / `[[nodiscard]] static constexpr EntityArchetypeId FromString(std::string_view value)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
