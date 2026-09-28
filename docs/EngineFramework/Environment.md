# Environment Goal 4 local audit

Status: B02 worker review complete. This document is module-local handoff evidence for Goal 4. It does not by itself promote the module or the whole Framework to canonical `LOCAL_READY`/`FROZEN`; serial integration and the official Windows/MSVC gates remain authoritative.

## Responsibility

Owns semantic gameplay environment layer types, blend handlers, hazard types and environment layers, including spatial indexing and semantic sampling. Runtime environment remains non-authoritative for this state.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Environment/include/Epidemic/GameFramework/Environment/environment.h`. The admission inventory in `docs/freeze/coverage_manifests.json` is the exact callable review input; B02 does not change public headers or public signatures.

## Dependency list

Direct production links are `EpidemicGameFrameworkFoundation` and `EpidemicFoundation`. No peer GameplayWorldStateOwner or Runtime module dependency is added by this delta.

## External ports/callbacks/providers/backends

Custom blend handlers are registered callbacks. Their failure, invalid result and exception are contained as controlled sample failures; no Runtime Environment object is owned here.

## Authoritative state

Type/blend/hazard registries; layer map; spatial cell index; global and large-layer lists; layer ID generator; revision; bounded environment change journal and diagnostics.

## Derived/cache/index state

spatial_index_, global_layers_ and large_layers_ are derived from layers_. Update/remove/restore rebuild or maintain every index consistently.

## ID spaces, generations, revisions and cursors

EnvironmentLayerId is service-generated or caller-supplied in-scope with generator advancement. Revision and change cursor are monotonic and restore carries a new journal epoch.

## State machines

Definition registries are mutable until Freeze. Layers support add, update, explicit expire/remove and time-based sweep. Expired layers do not remain authoritative.

## Persistent and transient state

Registry/build-definition state is not serialized as reusable runtime identity. Snapshot carriers persist only the state described below; provider/backend pointers and derived indexes remain transient/rebuilt state.

## Snapshot/restore contract

EnvironmentSnapshot persists persistent layers, ID generator, revision and journal epoch/sequence continuity. Restore requires a frozen registry, validates layer types/hazards/blend references and candidate indexes before swap. Restore uses module-private `RestoreCandidateBuild` and `RestoreBeforeCommit` seams to prove that candidate-construction and publication-boundary failures preserve the previous live state.

## Threading contract

The current module is owner-thread/external-serialization gameplay state. No stronger thread-safety guarantee is introduced by Goal 4. Goal 6 remains responsible for engine-wide concurrency qualification.

## Public mutation API

Add/update validate enums, time intervals, normalized fields, bounds and references before state publication. Expire/sweep/remove update primary and spatial indexes atomically. Exact no-op updates preserve revision where applicable. Every admission mutator has success/no-op/precondition/failure decisions recorded in `_goal4_handoff/B02/coverage_reviews.json`.

## Read/query API for invariants

Read/query methods are used as invariant witnesses in `environment_gameplay_tests.cpp` and are anchored per callable in `_goal4_handoff/B02/public_api_anchors.json`. Queries do not acquire hidden ownership of external objects.

## Local invariants

One layer ID maps to one layer; every spatial index entry resolves to the same authoritative layer; samples are ordered deterministically by layer semantics; equal hazard types retain source provenance; runtime observations are not copied into a second authoritative store.

## Hard limits, budgets and complexity bounds

Spatial indexing separates global/large layers, time arithmetic and IDs are checked, journal retention is bounded, and malformed normalized values are rejected.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/environment_gameplay_tests.cpp` covers registration/freeze, scoped sampling, hazards/provenance, invalid enum/range input, spatial reindexing, requested-ID continuity, throwing/invalid blend callbacks, snapshot/restore, stale cursors, bounded journal and module-local restore fault atomicity. Target: `EpidemicGameFrameworkEnvironmentTests`. Portable audit execution used C++23 GCC with `-Wall -Wextra -Wpedantic -Werror` in Debug-like (`-O0 -g`) and Release-like (`-O2 -DNDEBUG`) configurations. The official CMake/MSVC Debug/Release run is intentionally left to serial integration because this worker environment is non-Windows and the project Win32 Platform target rejects configuration here.

## Callable-specific closure contracts

Each row is the reviewed contract for one public callable; the ID is the stable inventory key used by the Goal 4 evidence ledger.

- `05af9908fc359cc1` — `epidemic::gameplay::environment::EnvironmentService` / `void Freeze()noexcept`: performs the documented lifecycle transition and preserves state when that transition is not admissible.
- `09a3cc8c6a2992bc` — `epidemic::gameplay::environment::EnvironmentLayerId` / `static constexpr EnvironmentLayerId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `10c61ec38268e00b` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<EnvironmentSample> Sample(EnvironmentPosition position,GameplayTimePoint time,std::span<const GameplayObjectRef> scopes={})const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `2142fcb92c3b1883` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] bool IsFrozen()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `261108043082b199` — `epidemic::gameplay::environment::EnvironmentBlendHandlerId` / `[[nodiscard]] constexpr bool operator==(const EnvironmentBlendHandlerId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `2790df4fe7dc7633` — `epidemic::gameplay::environment::EnvironmentHazard` / `[[nodiscard]] bool operator==(const EnvironmentHazard&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `292217fb44e77bf7` — `epidemic::gameplay::environment::EnvironmentHazardTypeId` / `[[nodiscard]] constexpr bool operator==(const EnvironmentHazardTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `2b2d8aabbca5da0e` — `epidemic::gameplay::environment::EnvironmentBlendHandlerId` / `static constexpr EnvironmentBlendHandlerId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `2f9a75b7da4c112c` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] EnvironmentSnapshot CaptureSnapshot()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `33631bcabf586a86` — `epidemic::gameplay::environment::EnvironmentLayerTypeId` / `[[nodiscard]] constexpr auto operator<=>(const EnvironmentLayerTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `387d7f3b0af554e0` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<void> SweepExpired(GameplayTimePoint now,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `3fbcb6f582abc57f` — `epidemic::gameplay::environment::EnvironmentLayerTypeId` / `[[nodiscard]] constexpr bool operator==(const EnvironmentLayerTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `4151bfefccbedf10` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<void> RegisterBlendHandler(EnvironmentBlendHandlerId id,std::string canonical_name,BlendHandler handler);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `4182c07850cfdb63` — `epidemic::gameplay::environment::EnvironmentHazardTypeId` / `[[nodiscard]] constexpr auto operator<=>(const EnvironmentHazardTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `41c31ebdc41a5a09` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] EnvironmentDiagnostics GetDiagnostics()const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `446c197b7a3ed1a6` — `epidemic::gameplay::environment::EnvironmentLayerTypeId` / `static constexpr EnvironmentLayerTypeId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `4976a0083d026c56` — `epidemic::gameplay::environment::EnvironmentHazardTypeId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `4a9ccc1b59f14c52` — `epidemic::gameplay::environment::EnvironmentLayerId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `545172f93fcea065` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] Revision CurrentRevision()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `54db3b98b0e8bbd4` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<void> RegisterLayerType(EnvironmentLayerTypeId id,std::string canonical_name);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `560c74e232da42c6` — `epidemic::gameplay::environment::EnvironmentService` / `EnvironmentService();`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `57d6c75ef86c2bbc` — `epidemic::gameplay::environment::EnvironmentHazardTypeId` / `static constexpr EnvironmentHazardTypeId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `587830b24900b9da` — `epidemic::gameplay::environment::EnvironmentLayer` / `[[nodiscard]] bool operator==(const EnvironmentLayer&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5c2811bc7cd4a4ff` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<EnvironmentLayerId> AddLayer(EnvironmentLayer layer);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `61dbdba6ba0fe9c3` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] std::vector<EnvironmentLayer> FindLayers(EnvironmentPosition position)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `767d558f85cf8465` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<void> UpdateLayer(EnvironmentLayer layer);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `76b0f79d561b5d68` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] std::optional<EnvironmentLayer> GetLayer(EnvironmentLayerId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `76c95995062e7ee8` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<void> RegisterHazardType(EnvironmentHazardTypeId id,std::string canonical_name);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `7cb3476238fe6955` — `epidemic::gameplay::environment::EnvironmentAabb` / `[[nodiscard]] constexpr bool Contains(EnvironmentPosition p)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `880871f17b68e141` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a9c4acc4a5f58beb` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<void> ExpireLayer(EnvironmentLayerId id,GameplayTimePoint now,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `aa088f3d04b390ff` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<void> RemoveLayer(EnvironmentLayerId id,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `ad9766e9991380e7` — `epidemic::gameplay::environment::EnvironmentLayerTypeId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `b98ac351ee0f0f8d` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `be296d2f8cada581` — `epidemic::gameplay::environment::EnvironmentLayerId` / `[[nodiscard]] constexpr auto operator<=>(const EnvironmentLayerId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `c43ea0df5a6285bb` — `epidemic::gameplay::environment::EnvironmentBlendHandlerId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `c5813db97ca73f52` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] foundation::Result<void> RestoreSnapshot(EnvironmentSnapshot snapshot);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `d01ffe865b5e0819` — `epidemic::gameplay::environment::EnvironmentValues` / `[[nodiscard]] constexpr bool operator==(const EnvironmentValues&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d28becf3663a9f1c` — `epidemic::gameplay::environment::EnvironmentLayerId` / `[[nodiscard]] constexpr bool operator==(const EnvironmentLayerId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d3189b15bed6dc4e` — `epidemic::gameplay::environment::EnvironmentAabb` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d848b08037cb00da` — `epidemic::gameplay::environment::EnvironmentSampleHazard` / `[[nodiscard]] bool operator==(const EnvironmentSampleHazard&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e376f84f64b9c5bf` — `epidemic::gameplay::environment::EnvironmentAabb` / `[[nodiscard]] constexpr bool operator==(const EnvironmentAabb&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f1e1310ff217e04c` — `epidemic::gameplay::environment::EnvironmentService` / `[[nodiscard]] EnvironmentChangeBatch ReadChangesSince(ChangeCursor cursor)const`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f6c32868d053d17d` — `epidemic::gameplay::environment::EnvironmentPosition` / `[[nodiscard]] constexpr bool operator==(const EnvironmentPosition&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
