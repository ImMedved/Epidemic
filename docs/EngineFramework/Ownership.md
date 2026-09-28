# Ownership Goal 4 local audit

Status: B02 worker review complete. This document is module-local handoff evidence for Goal 4. It does not by itself promote the module or the whole Framework to canonical `LOCAL_READY`/`FROZEN`; serial integration and the official Windows/MSVC gates remain authoritative.

## Responsibility

Owns semantic ownership records, permissions, access rules and property claims, plus all indexes, identity generators, revision and ownership journal.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Ownership/include/Epidemic/GameFramework/Ownership/ownership.h`. The admission inventory in `docs/freeze/coverage_manifests.json` is the exact callable review input; B02 does not change public headers or public signatures.

## Dependency list

Direct production links are `EpidemicGameFrameworkFoundation` and `EpidemicFoundation`. No peer GameplayWorldStateOwner or Runtime module dependency is added by this delta.

## External ports/callbacks/providers/backends

IRoleProvider is a non-owning authorization read port. No external durable transaction/backend is called by this module.

## Authoritative state

Ownership, grant, rule and claim maps; canonical owner and per-property/per-owner/per-subject indexes; four ID generators; revision; bounded journal; role provider pointer.

## Derived/cache/index state

canonical_owners_, ownership_by_property_, ownership_by_owner_, grants_by_subject_, rules_by_property_ and claims_by_property_ are derived indexes over their authoritative record maps.

## ID spaces, generations, revisions and cursors

OwnershipRecordId, PermissionGrantId, AccessRuleId and PropertyClaimId have independent service scopes. Expected Revision on transfer prevents stale replacement; journal cursor is monotonic and prunable.

## State machines

No registry Freeze state. Records are created/updated/transferred/removed; grants expire/revoke, rules update/remove, claims create/resolve. Terminal removal invalidates the corresponding typed ID.

## Persistent and transient state

Registry/build-definition state is not serialized as reusable runtime identity. Snapshot carriers persist only the state described below; provider/backend pointers and derived indexes remain transient/rebuilt state.

## Snapshot/restore contract

OwnershipSnapshot persists all four record sets, four generators, revision and journal continuity. Restore validates duplicate canonical ownership, record references/generator position and rebuilds every secondary index before swap. Restore uses module-private `RestoreCandidateBuild` and `RestoreBeforeCommit` seams to prove that candidate-construction and publication-boundary failures preserve the previous live state.

## Threading contract

The current module is owner-thread/external-serialization gameplay state. No stronger thread-safety guarantee is introduced by Goal 4. Goal 6 remains responsible for engine-wide concurrency qualification.

## Public mutation API

Assign/transfer/remove enforce one canonical owner per property/domain and stale expected-revision checks. Permission/rule/claim mutators publish journal changes before destructive removal where needed. Restore and multi-index operations use staged containers for failure atomicity. Every admission mutator has success/no-op/precondition/failure decisions recorded in `_goal4_handoff/B02/coverage_reviews.json`.

## Read/query API for invariants

Read/query methods are used as invariant witnesses in `ownership_tests.cpp` and are anchored per callable in `_goal4_handoff/B02/public_api_anchors.json`. Queries do not acquire hidden ownership of external objects.

## Local invariants

Canonical ownership contains no duplicate property/domain owner; every secondary index resolves to an existing authoritative record; transfers change from/to atomically; permission/rule/claim queries cannot expose removed entries; journal/revision do not falsely advance on rejection.

## Hard limits, budgets and complexity bounds

Four generators, revision and journal sequence are checked. Permission expiry sweep and journal prune are bounded by record count/retention policy. No fractional ownership model is exposed by the current public contract.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/ownership_tests.cpp` covers assignment/transfer/remove, stale expected revision, permission expiry, access rules, claims, snapshot duplicate/generator rejection, journal gap/prune and module-local restore fault atomicity. Target: `EpidemicGameFrameworkOwnershipTests`. Portable audit execution used C++23 GCC with `-Wall -Wextra -Wpedantic -Werror` in Debug-like (`-O0 -g`) and Release-like (`-O2 -DNDEBUG`) configurations. The official CMake/MSVC Debug/Release run is intentionally left to serial integration because this worker environment is non-Windows and the project Win32 Platform target rejects configuration here.

## Callable-specific closure contracts

Each row is the reviewed contract for one public callable; the ID is the stable inventory key used by the Goal 4 evidence ledger.

- `01ce3b119d159d71` — `epidemic::gameplay::ownership::AccessRuleId` / `static constexpr AccessRuleId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `01d03b940f983316` — `epidemic::gameplay::ownership::AccessRuleId` / `[[nodiscard]] constexpr auto operator<=>(const AccessRuleId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `01f9943c5e343339` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] std::vector<AccessRule> FindAccessRules(GameplayObjectRef property)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `03ff99204ee44893` — `epidemic::gameplay::ownership::IOwnershipRoleProvider` / `virtual ~IOwnershipRoleProvider()=default;`: implements the callable-specific behavior declared by this signature while preserving the module ownership and failure invariants.
- `0563e6f8e0f4be44` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] OwnershipPermissionResult EvaluatePermission(OwnershipPermissionQuery query)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `0a7a6e55f45a0653` — `epidemic::gameplay::ownership::PropertyRightId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `0f1015d6972a0ebb` — `epidemic::gameplay::ownership::RefHash` / `[[nodiscard]] std::size_t operator()(GameplayObjectRef r)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `0f8ccb32c1ae863e` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `149fe6540ac8b81a` — `epidemic::gameplay::ownership::OwnershipService` / `void SetRoleProvider(const IOwnershipRoleProvider*provider)noexcept`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `15ed35e68726168f` — `epidemic::gameplay::ownership::IOwnershipRoleProvider` / `[[nodiscard]] virtual bool HasRole(GameplayObjectRef subject,TypeId role,const GameplayContext&context)const noexcept=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1c984121af698a75` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] Revision CurrentRevision()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1f43040f9a434f9b` — `epidemic::gameplay::ownership::OwnershipRecordId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `22d067308fa35a15` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<void> UpdateAccessRule(AccessRule rule,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `24b3be2b269c50f4` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `24cb7581979f3f7e` — `epidemic::gameplay::ownership::PropertyDomainId` / `[[nodiscard]] constexpr auto operator<=>(const PropertyDomainId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `26b776bdf6e84362` — `epidemic::gameplay::ownership::PropertyRightId` / `static constexpr PropertyRightId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `2b718abd3a51b061` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<void> RestoreSnapshot(OwnershipSnapshot snapshot);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `2dbf23e1581f4ded` — `epidemic::gameplay::ownership::PermissionGrantId` / `static constexpr PermissionGrantId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `2fa87bfd74a87c0c` — `epidemic::gameplay::ownership::OwnershipRecordId` / `static constexpr OwnershipRecordId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `2fc79b385784b189` — `epidemic::gameplay::ownership::PermissionGrantId` / `[[nodiscard]] constexpr bool operator==(const PermissionGrantId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `334ba7a61a189a50` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] OwnershipDiagnostics GetDiagnostics()const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `3389ed21b401ad5c` — `std::hash` / `size_t operator()(const epidemic::gameplay::ownership::PropertyClaimId&v)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `349efb3b34eaead5` — `epidemic::gameplay::ownership::PropertyRightId` / `[[nodiscard]] constexpr bool operator==(const PropertyRightId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `361d2a436ddad2f3` — `epidemic::gameplay::ownership::OwnershipRecordId` / `[[nodiscard]] constexpr auto operator<=>(const OwnershipRecordId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `383cddc5eef3746b` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] std::size_t SweepExpiredPermissions(GameplayTimePoint now,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `3c603c7e16d28f83` — `epidemic::gameplay::ownership::PropertyClaimId` / `[[nodiscard]] constexpr bool operator==(const PropertyClaimId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `42d930c75b6161c7` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<PermissionGrantId> GrantPermission(PermissionGrant grant);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `438afaca0ba1c0fb` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] OwnershipSnapshot CaptureSnapshot()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `56a0c592ffa7f58f` — `epidemic::gameplay::ownership::PropertyDomainId` / `[[nodiscard]] constexpr bool operator==(const PropertyDomainId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5cb795e3cda901cd` — `epidemic::gameplay::ownership::OwnershipService` / `void PruneChangesThrough(std::uint64_t sequence);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `5fcaa1df8adcf6d7` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] OwnershipChangeBatch ReadChangesSince(ChangeCursor cursor)const`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `617d99d863bf2cd5` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<void> ResolveClaim(PropertyClaimId id,GameplayContext context={});`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `62312f06fe8184ba` — `epidemic::gameplay::ownership::PropertyRightId` / `[[nodiscard]] constexpr auto operator<=>(const PropertyRightId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `62bc2109effdd3de` — `epidemic::gameplay::ownership::PermissionGrantId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `687100b53170c64b` — `epidemic::gameplay::ownership::PropertyDomainId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `6d7dd9d16ed53f70` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<void> RevokePermission(PermissionGrantId id,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `71ca84d87ce5dd81` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] const OwnershipRecord*GetOwner(GameplayObjectRef property,PropertyDomainId domain)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `7c28a7de12548b36` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] std::vector<OwnershipRecord> FindPropertiesOwnedBy(GameplayObjectRef owner)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `89b3895ed4ff3139` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] std::vector<OwnershipRecord> GetOwnershipRecords(GameplayObjectRef property)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `8c03b17e566606cd` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] std::vector<PermissionGrant> FindPermissions(GameplayObjectRef subject)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a1d384299da4c78c` — `epidemic::gameplay::ownership::AccessRuleId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a3f20d0f65edfa78` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] const OwnershipRecord*GetOwner(GameplayObjectRef property)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `b43edc129d7e02dd` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] std::vector<PropertyClaim> FindClaims(GameplayObjectRef property)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ba3c40af40d7fc34` — `epidemic::gameplay::ownership::PropertyDomainId` / `static constexpr PropertyDomainId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `ba5a2e67b3d012b3` — `epidemic::gameplay::ownership::AccessRuleId` / `static constexpr AccessRuleId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `be4a5fed3912e976` — `std::hash` / `size_t operator()(const epidemic::gameplay::ownership::PermissionGrantId&v)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `bf162b4727d2632d` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<OwnershipRecordId> AssignOwnership(OwnershipRecord record);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `bf5930613334e171` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<void> RemoveOwnership(OwnershipRecordId id,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `c690a5794eaead20` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<AccessRuleId> AddAccessRule(AccessRule rule);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `c77000bea97063b9` — `epidemic::gameplay::ownership::PropertyClaimId` / `[[nodiscard]] constexpr auto operator<=>(const PropertyClaimId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `c98e01f66f19f3cb` — `epidemic::gameplay::ownership::PermissionGrantId` / `static constexpr PermissionGrantId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `d38b3abaedee851a` — `epidemic::gameplay::ownership::OwnershipRecordId` / `[[nodiscard]] constexpr bool operator==(const OwnershipRecordId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d6208512dab5e825` — `epidemic::gameplay::ownership::PropertyClaimId` / `static constexpr PropertyClaimId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `da1c4d64f8636e16` — `epidemic::gameplay::ownership::PropertyClaimId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `dd7b400490b23c01` — `epidemic::gameplay::ownership::OwnershipRecordId` / `static constexpr OwnershipRecordId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `df44720d2be655e7` — `std::hash` / `size_t operator()(const epidemic::gameplay::ownership::OwnershipRecordId&v)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e68ce7c7d18acb58` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<void> RemoveAccessRule(AccessRuleId id,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `ea914e42ec96d665` — `epidemic::gameplay::ownership::AccessRuleId` / `[[nodiscard]] constexpr bool operator==(const AccessRuleId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f7caa75a366a88bc` — `epidemic::gameplay::ownership::PermissionGrantId` / `[[nodiscard]] constexpr auto operator<=>(const PermissionGrantId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `fb8535949882e19a` — `epidemic::gameplay::ownership::PropertyClaimId` / `static constexpr PropertyClaimId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `fb8cc7e388c1e560` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<PropertyClaimId> CreateClaim(PropertyClaim claim);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `fe8dbfba6da9fca7` — `std::hash` / `size_t operator()(const epidemic::gameplay::ownership::AccessRuleId&v)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `fe8fd4b5c4a04958` — `epidemic::gameplay::ownership::OwnershipService` / `[[nodiscard]] foundation::Result<void> TransferOwnership(TransferOwnershipRequest request);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
