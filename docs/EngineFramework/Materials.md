# Materials Goal 4 local audit

Status: B02 worker review complete. This document is module-local handoff evidence for Goal 4. It does not by itself promote the module or the whole Framework to canonical `LOCAL_READY`/`FROZEN`; serial integration and the official Windows/MSVC gates remain authoritative.

## Responsibility

Owns material, substance, slot and reaction definitions plus per-subject material slot state, composition, contained substances, exposures and reaction-derived responses.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Materials/include/Epidemic/GameFramework/Materials/materials.h`. The admission inventory in `docs/freeze/coverage_manifests.json` is the exact callable review input; B02 does not change public headers or public signatures.

## Dependency list

Direct production links are `EpidemicGameFrameworkFoundation` and `EpidemicFoundation`. No peer GameplayWorldStateOwner or Runtime module dependency is added by this delta.

## External ports/callbacks/providers/backends

No external backend or retained callback. GameplayTagRegistry is read during tag-qualified reaction evaluation.

## Authoritative state

Four registries (materials, substances, slots, reactions), deterministic reaction order, subject-slot state map, revision and bounded material change journal.

## Derived/cache/index state

reaction_order_ is a sorted derived index over reaction definitions; subject/slot key mapping is authoritative for material state. Query projections are rebuilt from these containers.

## ID spaces, generations, revisions and cursors

Definition IDs are canonical-name-derived typed IDs. Per-state mutations share a module Revision and ChangeCursor epoch/sequence.

## State machines

Registries are mutable until Freeze, then definition registration is rejected. Material slot state appears on first successful mutation and can be removed by slot or subject.

## Persistent and transient state

Registry/build-definition state is not serialized as reusable runtime identity. Snapshot carriers persist only the state described below; provider/backend pointers and derived indexes remain transient/rebuilt state.

## Snapshot/restore contract

MaterialsSnapshot persists authoritative per-subject slot states, revision and journal continuity. Definitions are build-time registry state and must already be frozen before restore. Restore validates registered cross-references and builds a candidate state before commit. Restore uses module-private `RestoreCandidateBuild` and `RestoreBeforeCommit` seams to prove that candidate-construction and publication-boundary failures preserve the previous live state.

## Threading contract

The current module is owner-thread/external-serialization gameplay state. No stronger thread-safety guarantee is introduced by Goal 4. Goal 6 remains responsible for engine-wide concurrency qualification.

## Public mutation API

Composition is validated and normalized to the exact composition unit. Slot assignment, contents, exposures and stimuli reject unknown IDs and invalid quantities. Re-applying identical state is a no-op. Reaction evaluation does not replace primary composition with derived response state. Every admission mutator has success/no-op/precondition/failure decisions recorded in `_goal4_handoff/B02/coverage_reviews.json`.

## Read/query API for invariants

Read/query methods are used as invariant witnesses in `materials_tests.cpp` and are anchored per callable in `_goal4_handoff/B02/public_api_anchors.json`. Queries do not acquire hidden ownership of external objects.

## Local invariants

Each MaterialSlotKey has at most one state; every material/substance/slot reference is registered; composition fractions are normalized and nonnegative; reaction ordering is deterministic by priority/id; failed mutations do not expose a partial state/journal update.

## Hard limits, budgets and complexity bounds

Checked integer arithmetic and fixed composition PPM domain bound normalized composition. Revision/change sequence boundaries reject publication instead of wrapping; journal retention is bounded.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/materials_tests.cpp` covers registry freeze/duplicates, normalization, slot/content/exposure mutations, stimulus/reaction behavior, no-op revision stability, filtered snapshots, invalid restore, module-local restore fault atomicity and sparse-state stress. Target: `EpidemicGameFrameworkMaterialsTests`. Portable audit execution used C++23 GCC with `-Wall -Wextra -Wpedantic -Werror` in Debug-like (`-O0 -g`) and Release-like (`-O2 -DNDEBUG`) configurations. The official CMake/MSVC Debug/Release run is intentionally left to serial integration because this worker environment is non-Windows and the project Win32 Platform target rejects configuration here.

## Callable-specific closure contracts

Each row is the reviewed contract for one public callable; the ID is the stable inventory key used by the Goal 4 evidence ledger.

- `01f87586f880a09c` — `epidemic::gameplay::materials::SubstanceId` / `[[nodiscard]] static constexpr SubstanceId FromString(std::string_view name)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `06969b48e3da8cbb` — `epidemic::gameplay::materials::MaterialResponseTypeId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `06dcb20b81ad6dbf` — `epidemic::gameplay::materials::MaterialResponseTypeId` / `[[nodiscard]] constexpr bool operator==(const MaterialResponseTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `08a3f262a956d512` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<void> AssignComposition(GameplayObjectRef subject,MaterialSlotId slot,MaterialComposition composition,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `115cde2c14ab168a` — `epidemic::gameplay::materials::SubstanceQuantity` / `[[nodiscard]] constexpr bool operator==(const SubstanceQuantity&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1615e90ece6d1339` — `epidemic::gameplay::materials::MaterialSlotId` / `[[nodiscard]] constexpr bool operator==(const MaterialSlotId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `16ec347666c8f42c` — `epidemic::gameplay::materials::MaterialConstituent` / `[[nodiscard]] constexpr bool operator==(const MaterialConstituent&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1742efbe3eb55df6` — `epidemic::gameplay::materials::MaterialSlotKeyHash` / `[[nodiscard]] std::size_t operator()(const MaterialSlotKey&key)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `18a9e8486ede9f6e` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] MaterialsSnapshot CaptureSnapshot(std::span<const GameplayObjectRef> subjects)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1c9561e9421820b1` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<void> RemoveContainedSubstance(GameplayObjectRef subject,MaterialSlotId slot,SubstanceId substance,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `1f5feb2b03d56c7e` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<std::uint64_t> RemoveSubject(GameplayObjectRef subject,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `30937e1862f9cf16` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] std::vector<MaterialSlotState> FindByMaterialTag(TagId tag,const GameplayTagRegistry&tags)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `3238dc35bdf73386` — `epidemic::gameplay::materials::MaterialId` / `[[nodiscard]] constexpr bool operator==(const MaterialId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `3b827aefc746a1e3` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] bool IsSlotRegistered(MaterialSlotId id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `43e649b12d574f9b` — `epidemic::gameplay::materials::SubstanceAmount` / `constexpr SubstanceAmount()noexcept=default;`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `443e3bbeff55078b` — `epidemic::gameplay::materials::MaterialReactionId` / `[[nodiscard]] constexpr bool operator==(const MaterialReactionId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `4ba23fa4d091b511` — `epidemic::gameplay::materials::MaterialReactionId` / `[[nodiscard]] static constexpr MaterialReactionId FromString(std::string_view name)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `4bcd1ec8116c788b` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<MaterialSlotId> RegisterSlot(std::string_view canonical_name);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `511633b8c802cf3d` — `epidemic::gameplay::materials::MaterialService` / `void PruneChangesBefore(std::uint64_t sequence);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `52bdf7880d103324` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `54125c4cf69cf130` — `epidemic::gameplay::materials::MaterialSlotId` / `[[nodiscard]] constexpr auto operator<=>(const MaterialSlotId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5790d79e34638cf7` — `epidemic::gameplay::materials::MaterialSlotId` / `[[nodiscard]] static constexpr MaterialSlotId FromString(std::string_view name)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `5af4789f6636dacc` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<std::vector<MaterialResponse>> EvaluateStimulus(const MaterialStimulus&stimulus,const GameplayTagRegistry&tags)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5b377edbe3568dfb` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] const SubstanceDefinition*FindSubstance(SubstanceId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5f358bdded8fec6e` — `epidemic::gameplay::materials::MaterialSlotId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `628d92ef4ad75e53` — `epidemic::gameplay::materials::MaterialReactionId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `64cc3dc7e771ca97` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] std::optional<MaterialSlotState> FindState(GameplayObjectRef subject,MaterialSlotId slot)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `710f66ef794c6ac0` — `epidemic::gameplay::materials::DynamicMaterialState` / `[[nodiscard]] constexpr bool operator==(const DynamicMaterialState&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `737d8f637e704fe5` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<MaterialReactionId> RegisterReaction(MaterialReactionRule rule);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `73bdf821b6b70cae` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<void> ApplySubstanceExposure(GameplayObjectRef subject,MaterialSlotId slot,SubstanceId substance,SubstanceAmount amount,std::uint32_t coverage_ppm,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `80b93d716a0b3134` — `epidemic::gameplay::materials::MaterialService` / `void Freeze()noexcept`: performs the documented lifecycle transition and preserves state when that transition is not admissible.
- `8975aadf1bf2110a` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<SubstanceId> RegisterSubstance(SubstanceDefinition definition);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `8e8dc3f168f1c2be` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] const MaterialDefinition*FindMaterial(MaterialId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `904fe30c0d2c9f00` — `epidemic::gameplay::materials::MaterialResponseTypeId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `91c2c36987ef5cbe` — `epidemic::gameplay::materials::SubstanceId` / `[[nodiscard]] constexpr auto operator<=>(const SubstanceId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `924a66ad81338a61` — `epidemic::gameplay::materials::SubstanceAmount` / `[[nodiscard]] constexpr bool operator==(const SubstanceAmount&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `9384cd10726b409c` — `epidemic::gameplay::materials::MaterialId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `9559e83efd1a5c76` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<void> RestoreSnapshot(MaterialsSnapshot snapshot);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `9d4fd88b9794c857` — `epidemic::gameplay::materials::SubstanceExposure` / `[[nodiscard]] constexpr bool operator==(const SubstanceExposure&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a046026f0add6d3b` — `epidemic::gameplay::materials::MaterialResponseTypeId` / `[[nodiscard]] constexpr auto operator<=>(const MaterialResponseTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a988b031ead016b7` — `epidemic::gameplay::materials::SubstanceId` / `[[nodiscard]] constexpr bool operator==(const SubstanceId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a994a4a38abffc8d` — `epidemic::gameplay::materials::MaterialSlotKey` / `[[nodiscard]] constexpr bool operator==(const MaterialSlotKey&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ac1937b7a4f83263` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `b69057f69ecf1361` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] bool IsFrozen()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `b7b37c33a1d57b51` — `epidemic::gameplay::materials::TypeHash` / `template <typename T> [[nodiscard]] std::size_t operator()(const T&id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `b8283af8384bd3bd` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] MaterialsSnapshot CaptureSnapshot()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `bf210a8568bdb1f9` — `epidemic::gameplay::materials::MaterialReactionId` / `[[nodiscard]] constexpr auto operator<=>(const MaterialReactionId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `c3642926c27aa4e6` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<MaterialId> RegisterMaterial(MaterialDefinition definition);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `c7258a234569157c` — `epidemic::gameplay::materials::MaterialSlotId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ccf0dbf813ba32c2` — `epidemic::gameplay::materials::MaterialResponseTypeId` / `[[nodiscard]] static constexpr MaterialResponseTypeId FromString(std::string_view name)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `ce7f2e1fbcbd6510` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] Revision CurrentRevision()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `daab3e71950903e9` — `epidemic::gameplay::materials::SubstanceId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `dbfe3719a132f144` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<void> RemoveSlot(GameplayObjectRef subject,MaterialSlotId slot,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `e07643587b4dbaa6` — `epidemic::gameplay::materials::MaterialId` / `[[nodiscard]] static constexpr MaterialId FromString(std::string_view name)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `e0dff9f0dda0fe09` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] MaterialChangeBatch ReadChangesSince(ChangeCursor cursor)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e111a24f3f3c2413` — `epidemic::gameplay::materials::MaterialId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e1971db39431b16e` — `epidemic::gameplay::materials::MaterialId` / `[[nodiscard]] constexpr auto operator<=>(const MaterialId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e56121aaa2fb99d1` — `epidemic::gameplay::materials::SubstanceId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e9e8c9ef2f8bc286` — `epidemic::gameplay::materials::MaterialReactionId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ea4f0408b9b5efab` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<void> RemoveSubstanceExposure(GameplayObjectRef subject,MaterialSlotId slot,SubstanceId substance,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `eb0af9df67cac956` — `epidemic::gameplay::materials::SubstanceAmount` / `constexpr SubstanceAmount(std::int64_t value_micro)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `ec89bc5a97eec6f1` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<std::vector<MaterialResponse>> ApplyStimulus(const MaterialStimulus&stimulus,const GameplayTagRegistry&tags);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `ed563ec187439cab` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] std::vector<MaterialSlotState> FindSlots(GameplayObjectRef subject)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ee06ef3a2b32f098` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] MaterialsDiagnostics GetDiagnostics()const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f022b2fff741638c` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] std::vector<MaterialSlotState> FindExposedTo(SubstanceId substance)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f418f64ea405ce41` — `epidemic::gameplay::materials::MaterialService` / `[[nodiscard]] foundation::Result<void> SetContainedSubstance(GameplayObjectRef subject,MaterialSlotId slot,SubstanceId substance,SubstanceAmount amount,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `fd8ab893daeed29c` — `epidemic::gameplay::materials::SubstanceAmount` / `[[nodiscard]] constexpr bool IsPositive()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
