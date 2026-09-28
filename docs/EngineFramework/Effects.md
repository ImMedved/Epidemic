# Effects Goal 4 local audit

Status: B02 worker review complete. This document is module-local handoff evidence for Goal 4. It does not by itself promote the module or the whole Framework to canonical `LOCAL_READY`/`FROZEN`; serial integration and the official Windows/MSVC gates remain authoritative.

## Responsibility

Owns effect handler/definition registries, immediate effect execution orchestration, deferred effect records/schedule bindings, execution/deferred ID spaces, diagnostics and effect journal.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Effects/include/Epidemic/GameFramework/Effects/effects.h`. The admission inventory in `docs/freeze/coverage_manifests.json` is the exact callable review input; B02 does not change public headers or public signatures.

## Dependency list

Direct production links are `EpidemicGameFrameworkFoundation` and `EpidemicFoundation`. No peer GameplayWorldStateOwner or Runtime module dependency is added by this delta.

## External ports/callbacks/providers/backends

IEffectHandler and IEffectTargetStateProvider are non-owning ports. Handler Prepare runs before acceptance; handler Commit is treated as an accepted local/external side effect and post-commit framework bookkeeping must not require unbounded allocation.

## Authoritative state

Handler and definition registries; deferred record map; reverse schedule index; execution/deferred ID generators; diagnostics counters; bounded change journal.

## Derived/cache/index state

deferred_by_schedule_ is a reverse index over deferred_. It is preflighted/updated with deferred mutations and rebuilt from validated snapshot state.

## ID spaces, generations, revisions and cursors

Execution and deferred IDs use distinct MonotonicIdGenerator scopes. ChangeCursor epoch/sequence is checked and journal append failure forces snapshot resynchronization rather than false change publication.

## State machines

Handler/definition registry is mutable until Freeze. Execute is rejected before Freeze. Deferred requests progress through defer, schedule bind/clear, peek/take/acknowledge or cancellation. Restore is rejected before Freeze.

## Persistent and transient state

Registry/build-definition state is not serialized as reusable runtime identity. Snapshot carriers persist only the state described below; provider/backend pointers and derived indexes remain transient/rebuilt state.

## Snapshot/restore contract

EffectsSnapshot persists persistent deferred records, both ID generators, revision/journal continuity. Handler/definition registries are current-build state and must be frozen first. Restore validates records, IDs, schedules and generator position before commit. Restore uses module-private `RestoreCandidateBuild` and `RestoreBeforeCommit` seams to prove that candidate-construction and publication-boundary failures preserve the previous live state.

## Threading contract

The current module is owner-thread/external-serialization gameplay state. No stronger thread-safety guarantee is introduced by Goal 4. Goal 6 remains responsible for engine-wide concurrency qualification.

## Public mutation API

Registration and deferred publication have module-local pre-publication fault seams. Execute stages result/wave capacity and first-wave prepare before accepting the execution ID. Aggregate derived effects are admitted only when both per-parent and total max_effects budgets can contain them; exceeding the remaining total budget records BudgetExceeded without publishing more derived work. If framework-owned preparation of a later derived wave fails after an earlier handler commit, the accepted execution terminates as a successful Result carrying Failed batch disposition rather than returning a retryable top-level Failure. Every admission mutator has success/no-op/precondition/failure decisions recorded in `_goal4_handoff/B02/coverage_reviews.json`.

## Read/query API for invariants

Read/query methods are used as invariant witnesses in `effects_tests.cpp` and are anchored per callable in `_goal4_handoff/B02/public_api_anchors.json`. Queries do not acquire hidden ownership of external objects.

## Local invariants

Each schedule maps to at most one deferred effect and each bound record agrees with the reverse map; accepted execution IDs are never reused; a handler commit is not made retryable by a later framework-local staging failure or derived-work budget exhaustion; failed pre-accept/local storage operations do not publish an execution/deferred identity.

## Hard limits, budgets and complexity bounds

max_effects, max_waves and max_derived_per_parent are hard execution budgets. Counter increments saturate where overflow is observable, and journal retention is bounded.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp` covers handler/definition freeze, prepare/commit/provider failures, immediate/deferred flows, schedule binding/take/cancel, context propagation, execution budgets including aggregate derived-wave capacity, journal failure semantics, snapshot validation/pre-freeze rejection, candidate/pre-commit restore faults, pre-accept execution storage failure and post-commit derived-wave staging failure. Target: `EpidemicGameFrameworkEffectsTests`. Portable audit execution used C++23 GCC with `-Wall -Wextra -Wpedantic -Werror` in Debug-like (`-O0 -g`) and Release-like (`-O2 -DNDEBUG`) configurations. The official CMake/MSVC Debug/Release run is intentionally left to serial integration because this worker environment is non-Windows and the project Win32 Platform target rejects configuration here.

## Callable-specific closure contracts

Each row is the reviewed contract for one public callable; the ID is the stable inventory key used by the Goal 4 evidence ledger.

- `008b44939343070d` — `epidemic::gameplay::effects::EffectTypeId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `01dd044b7a9de2d9` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `07698bede104476e` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `0a31ef563670ca3a` — `epidemic::gameplay::effects::IEffectHandler` / `[[nodiscard]] virtual EffectHandlerCapabilities Capabilities()const noexcept=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `0e0f7f3a5aee4f1f` — `epidemic::gameplay::effects::RegisteredEffectPayload` / `template <typename T> [[nodiscard]] std::optional<T> AsTrivial(TypeId expected_type)const`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `0fff1944b9c304ab` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] EffectsDiagnostics GetDiagnostics()const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `11624722c61b493f` — `epidemic::gameplay::effects::DeferredEffectIdHash` / `[[nodiscard]] std::size_t operator()(const DeferredEffectId&id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `16f538ab6c7db8f4` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<void> AcknowledgeDeferredBySchedule(ScheduleId schedule,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `1c618acd4338c9c9` — `epidemic::gameplay::effects::EffectDefinitionId` / `[[nodiscard]] constexpr std::uint64_t Raw()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `2132e05c88165657` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<void> BindDeferredSchedule(DeferredEffectId id,ScheduleId schedule);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `2134cd89f3d7a0a5` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<void> ClearDeferredSchedule(DeferredEffectId id);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `232c755978c11abf` — `epidemic::gameplay::effects::EffectService` / `void PruneChangesBefore(std::uint64_t sequence);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `2d32751df7654969` — `epidemic::gameplay::effects::EffectTypeId` / `[[nodiscard]] constexpr bool operator==(const EffectTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `33b35f8d6afbcc45` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] std::uint64_t OldestChangeSequence()const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `361b9f31349f8126` — `epidemic::gameplay::effects::EffectExecutionId` / `[[nodiscard]] constexpr bool operator==(const EffectExecutionId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `37de9f717218a5e9` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] const EffectDefinition*FindDefinition(EffectDefinitionId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `486488a1df8f628f` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<EffectRequest> PeekDeferredBySchedule(ScheduleId schedule,GameplayContext context={})const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `4f5d1c21b3ad4a73` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<void> RestoreSnapshot(EffectsSnapshot snapshot);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `50985da2ea91f2c2` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] std::optional<DeferredEffectRecord> FindDeferredCopy(DeferredEffectId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `50cf131048a8eb8a` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] std::uint64_t CancelDeferredTargeting(GameplayObjectRef target,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `541faa5e4df950b7` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<EffectRequest> TakeDeferredBySchedule(ScheduleId schedule,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `57b35317517d72ef` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<void> CancelDeferred(DeferredEffectId id,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `5b3fec1832d1d1c0` — `epidemic::gameplay::effects::EffectTypeIdHash` / `[[nodiscard]] std::size_t operator()(EffectTypeId id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5b956e8eb7328d0f` — `epidemic::gameplay::effects::EffectTypeId` / `[[nodiscard]] static constexpr EffectTypeId FromString(std::string_view name)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `61b8a4ac1c002918` — `epidemic::gameplay::effects::DeferredEffectId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `6506e6c7ef71ad2f` — `epidemic::gameplay::effects::EffectExecutionId` / `[[nodiscard]] constexpr auto operator<=>(const EffectExecutionId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `6f6198d4ec641e67` — `epidemic::gameplay::effects::EffectDefinitionId` / `[[nodiscard]] constexpr bool operator==(const EffectDefinitionId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `86695555e1941c8d` — `epidemic::gameplay::effects::EffectTypeId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `87f8195dffe28ed8` — `epidemic::gameplay::effects::IEffectHandler` / `virtual ~IEffectHandler()=default;`: implements the callable-specific behavior declared by this signature while preserving the module ownership and failure invariants.
- `89d84fae7a7fb2e5` — `epidemic::gameplay::effects::DeferredEffectId` / `[[nodiscard]] constexpr auto operator<=>(const DeferredEffectId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `8a2cf3ae03feca99` — `epidemic::gameplay::effects::EffectService` / `void SetTargetStateProvider(const IEffectTargetStateProvider*provider)noexcept`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `92ef83b889331efd` — `epidemic::gameplay::effects::EffectDefinitionId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `935217737286f1db` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] EffectChangeBatch ReadChangesSince(ChangeCursor cursor)const`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a3f559379aadabce` — `epidemic::gameplay::effects::EffectDefinitionIdHash` / `[[nodiscard]] std::size_t operator()(EffectDefinitionId id)const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `a8cdabb98587cc16` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] const DeferredEffectRecord*FindDeferred(DeferredEffectId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `afaddd9bab7340a9` — `epidemic::gameplay::effects::EffectDefinitionId` / `[[nodiscard]] constexpr auto operator<=>(const EffectDefinitionId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `c136a014ee5a5d1b` — `epidemic::gameplay::effects::IEffectTargetStateProvider` / `virtual ~IEffectTargetStateProvider()=default;`: implements the callable-specific behavior declared by this signature while preserving the module ownership and failure invariants.
- `c23b578b7333b48c` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] std::vector<DeferredEffectRecord> AllDeferred()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `c5c72f2fa683cf73` — `epidemic::gameplay::effects::EffectDefinitionId` / `[[nodiscard]] static constexpr EffectDefinitionId FromString(std::string_view name)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `c7266b4003f6e907` — `epidemic::gameplay::effects::IEffectHandler` / `[[nodiscard]] virtual EffectTypeId Type()const noexcept=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d0b240f8ef994997` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] EffectsSnapshot CaptureSnapshot()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d276339907238d5f` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] std::vector<DeferredEffectRecord> UnscheduledDeferred()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d3c338117f941a5a` — `epidemic::gameplay::effects::IEffectHandler` / `[[nodiscard]] virtual foundation::Result<EffectCommitResult> Commit(const EffectOperation&operation,const RegisteredEffectPayload&commit_token)noexcept=0;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `d3e2f414a8ff9add` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] bool IsFrozen()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d727e6e1d2c4d6b6` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<DeferredEffectId> Defer(EffectRequest request,ClockId clock,GameplayTimePoint due,DeferredEffectPersistence persistence=DeferredEffectPersistence::Session);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `dd9627df6e9d5ef4` — `epidemic::gameplay::effects::IEffectTargetStateProvider` / `[[nodiscard]] virtual EffectTargetState Resolve(GameplayObjectRef target)const=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e2fa50a3d942c7c5` — `epidemic::gameplay::effects::EffectTypeId` / `[[nodiscard]] constexpr auto operator<=>(const EffectTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e5f957a4cc6dcf61` — `epidemic::gameplay::effects::DeferredEffectId` / `[[nodiscard]] constexpr bool operator==(const DeferredEffectId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e62a2e16f4602c3e` — `epidemic::gameplay::effects::RegisteredEffectPayload` / `[[nodiscard]] bool Empty()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e6482302c736d4f7` — `epidemic::gameplay::effects::RegisteredEffectPayload` / `template <typename T> [[nodiscard]] static RegisteredEffectPayload FromTrivial(TypeId type_id,const T&value)`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `e7f33605f4603c9a` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<EffectExecutionResult> Execute(EffectRequest request,EffectExecutionBudget budget={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `e83b57f80573871a` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<EffectDefinitionId> RegisterDefinition(EffectDefinition definition);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `e9ae26f75ae08a1e` — `epidemic::gameplay::effects::IEffectHandler` / `[[nodiscard]] virtual foundation::Result<EffectPrepareResult> Prepare(const EffectOperation&operation)const=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `eba49ac6a433869b` — `epidemic::gameplay::effects::EffectService` / `EffectService();`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `ec5341156b5c4e4f` — `epidemic::gameplay::effects::EffectExecutionId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f6a67d0ee2b8a519` — `epidemic::gameplay::effects::EffectService` / `void Freeze()noexcept`: performs the documented lifecycle transition and preserves state when that transition is not admissible.
- `fd595be5008cc778` — `epidemic::gameplay::effects::EffectService` / `[[nodiscard]] foundation::Result<EffectTypeId> RegisterHandler(std::string_view canonical_name,std::shared_ptr<IEffectHandler> handler,TypeId payload_type={},std::size_t max_payload_bytes=0,PayloadValidator validator={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
