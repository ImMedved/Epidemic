# Interaction Goal 4 local audit

Status: B02 worker review complete. This document is module-local handoff evidence for Goal 4. It does not by itself promote the module or the whole Framework to canonical `LOCAL_READY`/`FROZEN`; serial integration and the official Windows/MSVC gates remain authoritative.

## Responsibility

Owns interaction definitions, provider/executor registrations, active timed interaction sessions, schedule bindings, execution IDs, revision/journal state and interaction diagnostics.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Interaction/include/Epidemic/GameFramework/Interaction/interaction.h`. The admission inventory in `docs/freeze/coverage_manifests.json` is the exact callable review input; B02 does not change public headers or public signatures.

## Dependency list

Direct production links are `EpidemicGameFrameworkFoundation` and `EpidemicFoundation`. No peer GameplayWorldStateOwner or Runtime module dependency is added by this delta.

## External ports/callbacks/providers/backends

IInteractionProvider, IInteractionExecutor and IInteractionStateProvider are non-owning ports. Candidate collection and executor validation/commit failures are contained by the service contract; volatile materialization is rechecked at commit.

## Authoritative state

Definition registry; provider/executor maps; active session map; schedule-to-session reverse index; ID generator; revision; bounded journal; non-owning state provider.

## Derived/cache/index state

session_by_schedule_ derives from session records. Actor active-session queries derive from sessions_ and cannot become a second source of truth.

## ID spaces, generations, revisions and cursors

InteractionExecutionId is service-generated. Prepared plans carry actor/target revisions used for stale checks. Revision and journal cursor are monotonic and restored with epoch continuity.

## State machines

Definitions/providers/executors register before Freeze. Interaction flows candidate -> prepare -> commit, with immediate or active timed session outcome, then complete/cancel/timeout. Restore is rejected before Freeze.

## Persistent and transient state

Registry/build-definition state is not serialized as reusable runtime identity. Snapshot carriers persist only the state described below; provider/backend pointers and derived indexes remain transient/rebuilt state.

## Snapshot/restore contract

InteractionSnapshot persists eligible active sessions, ID generator, revision and journal state. Restore validates definitions/session mode/schedule bindings/generator bounds into candidate structures before replacing live sessions. Restore uses module-private `RestoreCandidateBuild` and `RestoreBeforeCommit` seams to prove that candidate-construction and publication-boundary failures preserve the previous live state.

## Threading contract

The current module is owner-thread/external-serialization gameplay state. No stronger thread-safety guarantee is introduced by Goal 4. Goal 6 remains responsible for engine-wide concurrency qualification.

## Public mutation API

Commit revalidates plan, revisions and materialization before executor side effects. Completion/cancellation/time sweep are terminal and duplicate terminal execution is rejected/no-op according to the method contract. Schedule bind validates one-to-one ownership. Every admission mutator has success/no-op/precondition/failure decisions recorded in `_goal4_handoff/B02/coverage_reviews.json`.

## Read/query API for invariants

Read/query methods are used as invariant witnesses in `interaction_gameplay_tests.cpp` and are anchored per callable in `_goal4_handoff/B02/public_api_anchors.json`. Queries do not acquire hidden ownership of external objects.

## Local invariants

An execution ID has at most one active session; a ScheduleId binds at most one active execution; provider/executor pointers are registry references rather than owned state; failed validation/provider/restore paths preserve prior session/revision state.

## Hard limits, budgets and complexity bounds

Execution IDs, revisions and journal sequence are checked; bounded journal reports stale cursors. Timed completion uses checked gameplay time semantics.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/interaction_gameplay_tests.cpp` covers provider exception containment, prepare revision capture, materialization recheck, timed commit/complete, duplicate execution prevention through active session state, pre-freeze restore rejection and module-local restore fault atomicity. Target: `EpidemicGameFrameworkInteractionTests`. Portable audit execution used C++23 GCC with `-Wall -Wextra -Wpedantic -Werror` in Debug-like (`-O0 -g`) and Release-like (`-O2 -DNDEBUG`) configurations. The official CMake/MSVC Debug/Release run is intentionally left to serial integration because this worker environment is non-Windows and the project Win32 Platform target rejects configuration here.

## Callable-specific closure contracts

Each row is the reviewed contract for one public callable; the ID is the stable inventory key used by the Goal 4 evidence ledger.

- `01988277f66040f4` — `epidemic::gameplay::interaction::InteractionTypeId` / `[[nodiscard]] constexpr bool operator==(const InteractionTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `01b873075f4be3ea` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] InteractionChangeBatch ReadChangesSince(ChangeCursor cursor)const`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `0bd65e078e747d77` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] std::optional<InteractionSession> FindSessionCopy(InteractionExecutionId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `10fa4ffd2c45a30e` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `12c19233d3443276` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] std::vector<InteractionCandidate> GetAvailableInteractions(const InteractionContext&context)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1fd449cc95d55321` — `epidemic::gameplay::interaction::InteractionTypeId` / `static constexpr InteractionTypeId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `219fdb3b946e3941` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<void> Cancel(InteractionExecutionId id,TypeId reason={},GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `2ad76d2a2e4a114d` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] std::vector<InteractionSession> SessionsRequiringSchedule()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `3127b1778d1b6d26` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<InteractionPlan> Prepare(const InteractionContext&context,InteractionCandidate candidate)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `35045263db8c8b6c` — `epidemic::gameplay::interaction::InteractionExecutionId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `373f7d4dc5cbfea9` — `epidemic::gameplay::interaction::InteractionProviderId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `39c407c70838dc96` — `epidemic::gameplay::interaction::InteractionService` / `void Freeze()noexcept`: performs the documented lifecycle transition and preserves state when that transition is not admissible.
- `40a1e000939e3aa2` — `epidemic::gameplay::interaction::IInteractionProvider` / `[[nodiscard]] virtual std::vector<InteractionCandidate> Collect(const InteractionContext&context)const=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `432f4c501e663ae4` — `epidemic::gameplay::interaction::IInteractionStateProvider` / `virtual ~IInteractionStateProvider()=default;`: implements the callable-specific behavior declared by this signature while preserving the module ownership and failure invariants.
- `4573113bb75ed1b8` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<InteractionResult> Commit(const InteractionPlan&plan);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `4eea44efd2660881` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] const InteractionDefinition*FindDefinition(InteractionTypeId type)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `4fe6abaa2119e818` — `epidemic::gameplay::interaction::IInteractionStateProvider` / `[[nodiscard]] virtual bool IsMaterialized(GameplayObjectRef object)const=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `58151ffacc6a14b7` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] std::vector<InteractionSession> FindActive(GameplayObjectRef actor)const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `5ef9c6f2632ba871` — `epidemic::gameplay::interaction::IInteractionExecutor` / `virtual ~IInteractionExecutor()=default;`: implements the callable-specific behavior declared by this signature while preserving the module ownership and failure invariants.
- `65c0f82c4da104f1` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<void> Complete(InteractionExecutionId id,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `65d0f0d574e8182a` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] InteractionSnapshot CaptureSnapshot()const;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `6c56f7d3ed181bb5` — `epidemic::gameplay::interaction::IInteractionStateProvider` / `[[nodiscard]] virtual Revision RevisionOf(GameplayObjectRef object)const=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `7e5eae56185251f2` — `epidemic::gameplay::interaction::InteractionTypeId` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `7e6c6524226b150e` — `epidemic::gameplay::interaction::InteractionProviderId` / `[[nodiscard]] constexpr auto operator<=>(const InteractionProviderId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `7f06d522831b406e` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<void> RegisterProvider(const IInteractionProvider&provider);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `8d194ac52870a9b1` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] InteractionDiagnostics GetDiagnostics()const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `960a47cab0b0d608` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<void> RestoreSnapshot(InteractionSnapshot snapshot);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `a0176438f65b8856` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<void> RegisterDefinition(InteractionDefinition definition);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `a1a99f69097b0417` — `epidemic::gameplay::interaction::InteractionExecutionId` / `[[nodiscard]] constexpr auto operator<=>(const InteractionExecutionId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ae152f92fa0549e0` — `epidemic::gameplay::interaction::InteractionPoint` / `[[nodiscard]] constexpr bool operator==(const InteractionPoint&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `b2632b612c7dc0b1` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<void> RegisterExecutor(InteractionTypeId type,IInteractionExecutor&executor);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `b85bd8d7dfb314e5` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<void> BindCompletionSchedule(InteractionExecutionId id,ScheduleId schedule);`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `c049855a809b34f5` — `epidemic::gameplay::interaction::InteractionProviderId` / `static constexpr InteractionProviderId FromString(std::string_view s)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `ccaa861e6b0bb60e` — `epidemic::gameplay::interaction::InteractionService` / `void SetStateProvider(const IInteractionStateProvider*provider)noexcept`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `d97ba9f79fa222ec` — `epidemic::gameplay::interaction::InteractionExecutionId` / `[[nodiscard]] constexpr bool operator==(const InteractionExecutionId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `dea9fb3c6e38255c` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] bool IsFrozen()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e074c9a1fa7aad0b` — `epidemic::gameplay::interaction::IInteractionExecutor` / `[[nodiscard]] virtual foundation::Result<void> Commit(const InteractionPlan&plan,InteractionExecutionId execution)noexcept=0;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `e97f168fead6e746` — `epidemic::gameplay::interaction::IInteractionProvider` / `virtual ~IInteractionProvider()=default;`: implements the callable-specific behavior declared by this signature while preserving the module ownership and failure invariants.
- `ea7b7b3ce4684248` — `epidemic::gameplay::interaction::InteractionTypeId` / `[[nodiscard]] constexpr auto operator<=>(const InteractionTypeId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `eabb4927ea77d264` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] Revision CurrentRevision()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `edbb9ae038127c05` — `epidemic::gameplay::interaction::InteractionProviderId` / `[[nodiscard]] constexpr bool operator==(const InteractionProviderId&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `ef013b0f7b30fd78` — `epidemic::gameplay::interaction::InteractionService` / `InteractionService();`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `f0c8f7c7d0ba7ce4` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] foundation::Result<void> SweepTimed(GameplayTimePoint now,GameplayContext context={});`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `f1a2b0a6ea3f9f68` — `epidemic::gameplay::interaction::IInteractionProvider` / `[[nodiscard]] virtual InteractionProviderId Id()const noexcept=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f517de74f99ac746` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] const InteractionSession*FindSession(InteractionExecutionId id)const noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f6ce553a2e2ed57a` — `epidemic::gameplay::interaction::IInteractionExecutor` / `[[nodiscard]] virtual foundation::Result<void> Validate(const InteractionPlan&plan)const=0;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `f7bbe55e625fbae4` — `epidemic::gameplay::interaction::InteractionService` / `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
