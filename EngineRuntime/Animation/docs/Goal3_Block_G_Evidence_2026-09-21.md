# Goal 3 Block G evidence fragment: Animation

Date: 2026-09-21. This is module-local handoff evidence. Shared generated manifests, anchor registry and `local_ready_ledger.json` are intentionally unchanged. Final admission remains conditional on integrated MSVC Debug/Release qualification after merge.

## Public signature anchor proposals

The committed generated inventory contains 48 Animation public callables. The proposals are signature-keyed so the serial integrator can bind regenerated stable IDs after all parallel deltas are merged.

| Public declaration | Proposed contract anchor | Proposed test/audit anchor |
| --- | --- | --- |
| `epidemic::runtime::animation::ISkeletonRegistry::virtual ~ISkeletonRegistry()=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::ISkeletonRegistry::[[nodiscard]] virtual foundation::Result<void> RegisterSkeleton(SkeletonDesc desc)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::ISkeletonRegistry::[[nodiscard]] virtual bool HasSkeleton(SkeletonId id)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::ISkeletonRegistry::[[nodiscard]] virtual foundation::Result<void> Freeze()=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::ISkeletonRegistry::[[nodiscard]] virtual bool IsFrozen()const noexcept=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationClipRegistry::virtual ~IAnimationClipRegistry()=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationClipRegistry::[[nodiscard]] virtual foundation::Result<void> RegisterClip(AnimationClipDesc desc)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationClipRegistry::[[nodiscard]] virtual bool HasClip(AnimationClipId id)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationClipRegistry::[[nodiscard]] virtual foundation::Result<void> Freeze()=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationClipRegistry::[[nodiscard]] virtual bool IsFrozen()const noexcept=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::virtual ~IAnimationRuntime()=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<AnimatorHandle> CreateAnimatorHandle(const AnimatorDesc&desc)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestCreateDestroyAnimator; TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> DestroyAnimator(AnimatorHandle handle)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestCreateDestroyAnimator; TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Play(const AnimationPlaybackCommand&command)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestStateTransitionsAndEvents; TestPortablePlaybackScalingBoundaries; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Pause(AnimatorHandle handle)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestStateTransitionsAndEvents; TestPortablePlaybackScalingBoundaries; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Stop(AnimatorHandle handle)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestStateTransitionsAndEvents; TestPortablePlaybackScalingBoundaries; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Crossfade(AnimatorHandle handle,AnimationClipId clip,FrameDuration duration)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestStateTransitionsAndEvents; TestPortablePlaybackScalingBoundaries; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<std::size_t> Tick(FrameDuration delta,std::size_t max_animators)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestStateTransitionsAndEvents; TestPortablePlaybackScalingBoundaries; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<AnimatorSnapshot> GetAnimatorSnapshot(AnimatorHandle handle)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestHandlePlaybackPoseBufferAndFactory; TestPoseQueriesRejectStaleHandle` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> SetLod(AnimatorHandle handle,AnimationLodLevel lod)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestCreateDestroyAnimator; TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IPoseProvider::virtual ~IPoseProvider()=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IPoseProvider::[[nodiscard]] virtual foundation::Result<PoseState> GetPoseState(AnimatorHandle handle)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestHandlePlaybackPoseBufferAndFactory; TestPoseQueriesRejectStaleHandle` |
| `epidemic::runtime::animation::IPoseProvider::[[nodiscard]] virtual foundation::Result<PoseSnapshot> GetPoseSnapshot(AnimatorHandle handle)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestHandlePlaybackPoseBufferAndFactory; TestPoseQueriesRejectStaleHandle` |
| `epidemic::runtime::animation::IPoseProvider::[[nodiscard]] virtual foundation::Result<PoseBuffer> GetPoseBuffer(AnimatorHandle handle)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestHandlePlaybackPoseBufferAndFactory; TestPoseQueriesRejectStaleHandle` |
| `epidemic::runtime::animation::IAnimationResourceSource::virtual ~IAnimationResourceSource()=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationResourceSource::[[nodiscard]] virtual foundation::Result<SkeletonDesc> LoadSkeleton(SkeletonId id)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationResourceSource::[[nodiscard]] virtual foundation::Result<AnimationClipDesc> LoadClip(AnimationClipId id)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationPoseSink::virtual ~IAnimationPoseSink()=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationPoseSink::[[nodiscard]] virtual foundation::Result<void> Publish(std::shared_ptr<const PoseBuffer> pose)=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationEvaluatorBackend::virtual ~IAnimationEvaluatorBackend()=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationEvaluatorBackend::[[nodiscard]] virtual foundation::Result<PoseBuffer> EvaluatePose(const AnimationEvaluationRequest&request)const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationEventBuffer::virtual ~IAnimationEventBuffer()=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationEventBuffer::[[nodiscard]] virtual std::span<const AnimationEvent> Events()const=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestStateTransitionsAndEvents; TestEventCapacityDropsOldest; TestZeroEventCapacityUsesDefaultBound` |
| `epidemic::runtime::animation::IAnimationEventBuffer::virtual void Clear()=0;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestStateTransitionsAndEvents; TestEventCapacityDropsOldest; TestZeroEventCapacityUsesDefaultBound` |
| `epidemic::runtime::animation::[[nodiscard]] foundation::Result<AnimationServices> CreateAnimationServices(AnimationOptions options={},AnimationDependencies dependencies={});` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestFactoryProfilesSeparateMockEvaluation` |
| `epidemic::runtime::animation::[[nodiscard]] AnimationServices CreateReferenceAnimationServices(AnimationOptions options={});` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestFactoryProfilesSeparateMockEvaluation` |
| `epidemic::runtime::animation::[[nodiscard]] AnimationServices CreateMockAnimationServices(AnimationOptions options={});` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestFactoryProfilesSeparateMockEvaluation` |
| `epidemic::runtime::animation::SkeletonId::[[nodiscard]] constexpr bool IsValid()const noexcept` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::SkeletonId::[[nodiscard]] constexpr bool operator==(const SkeletonId&)const noexcept=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::AnimationClipId::[[nodiscard]] constexpr bool IsValid()const noexcept` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::AnimationClipId::[[nodiscard]] constexpr bool operator==(const AnimationClipId&)const noexcept=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::AnimatorInstanceId::[[nodiscard]] constexpr bool IsValid()const noexcept` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::AnimatorInstanceId::[[nodiscard]] constexpr bool operator==(const AnimatorInstanceId&)const noexcept=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::AnimatorHandle::[[nodiscard]] constexpr bool IsValid()const noexcept` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::AnimatorHandle::[[nodiscard]] constexpr bool operator==(const AnimatorHandle&)const noexcept=default;` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `std::hash::[[nodiscard]] size_t operator()(epidemic::runtime::animation::SkeletonId value)const noexcept` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `std::hash::[[nodiscard]] size_t operator()(epidemic::runtime::animation::AnimationClipId value)const noexcept` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `std::hash::[[nodiscard]] size_t operator()(epidemic::runtime::animation::AnimatorInstanceId value)const noexcept` | `docs/EngineRuntime/modules/animation.md::Контракты / Playback и владение` | `animation_tests.cpp::TestValidationFailures; TestDeepFreezeAnimationContracts` |

## Dossier field review decisions

| Field | Decision | Rationale |
| --- | --- | --- |
| `Responsibility` | `REVIEWED` | Module responsibility and forbidden cross-major ownership were checked against public headers, implementation and module documentation. |
| `Authoritative state` | `REVIEWED` | Authoritative mutable records and lifecycle/revision state were traced through all public mutators. |
| `Derived/cache/index state` | `REVIEWED` | Derived caches, staged state and lookup/index structures were checked for publication ordering and invalidation. |
| `Public mutation API` | `REVIEWED` | Every generated mutator family has precondition, success, failure and no-op semantics mapped to regressions. |
| `Read/query API for invariants` | `REVIEWED` | Queries return detached/validated observations and stale identity behavior is covered where applicable. |
| `Public headers and types` | `REVIEWED` | Public type domains, enum/numeric validation and factory surface were reviewed; Block G adds no public API. |
| `Dependency list` | `REVIEWED` | Only Foundation/RuntimeFoundation direct dependencies remain; no Runtime peer-major dependency was added. |
| `External ports/callbacks/providers/backends` | `REVIEWED` | External sink/backend/bridge boundaries and their Result/exception semantics were reviewed against targeted fault regressions. |
| `ID spaces, generations, revisions and cursors` | `REVIEWED` | Identity allocation, generation/revision exhaustion and stale handle paths are covered where the module owns them. |
| `State machines` | `REVIEWED` | Allowed, forbidden and terminal/recovery transitions are represented by explicit tests. |
| `Local invariants` | `REVIEWED` | Numeric domains, ownership, ordering and publication invariants were checked against implementation and tests. |
| `Persistent and transient state` | `REVIEWED` | These modules own transient runtime state; no new Goal 3 persistence contract is introduced. |
| `Snapshot/restore contract` | `REVIEWED` | No persistence restore surface exists locally; snapshot/query objects are observational and detached. |
| `Hard limits, budgets and complexity bounds` | `REVIEWED` | Capacity, ID/revision bounds, bounded events/queues and frame/work ordering limits are exercised by tests. |
| `Threading contract` | `REVIEWED` | No private worker ownership is introduced; mutable runtime use remains caller-serialized/synchronous. |

## Generated mutator obligation review

The current generated coverage input contains 56 mutator obligations for Animation. All generated `preconditions`, `success`, `failure` and `noop` records were reviewed by public mutator family against the module contract and the named regressions in the signature table. A scanner `noop` obligation is treated as defined behavior, not as a requirement that every mutator must succeed as a no-op. No shared coverage status is changed in this parallel delta.


## Stale-identity candidate review

Generated candidate count: 13.

| Candidate | Decision | Evidence |
| --- | --- | --- |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> SetLod(AnimatorHandle handle,AnimationLodLevel lod)=0;` | `REVIEWED / real stale-handle boundary` | `TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Stop(AnimatorHandle handle)=0;` | `REVIEWED / real stale-handle boundary` | `TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationClipRegistry::[[nodiscard]] virtual foundation::Result<void> Freeze()=0;` | `REVIEWED / scanner false positive for stale identity` | `TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::ISkeletonRegistry::[[nodiscard]] virtual foundation::Result<void> RegisterSkeleton(SkeletonDesc desc)=0;` | `REVIEWED / scanner false positive for stale identity` | `TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<AnimatorHandle> CreateAnimatorHandle(const AnimatorDesc&desc)=0;` | `REVIEWED / scanner false positive for stale identity` | `TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationClipRegistry::[[nodiscard]] virtual foundation::Result<void> RegisterClip(AnimationClipDesc desc)=0;` | `REVIEWED / scanner false positive for stale identity` | `TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> DestroyAnimator(AnimatorHandle handle)=0;` | `REVIEWED / real stale-handle boundary` | `TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Crossfade(AnimatorHandle handle,AnimationClipId clip,FrameDuration duration)=0;` | `REVIEWED / real stale-handle boundary` | `TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Play(const AnimationPlaybackCommand&command)=0;` | `REVIEWED / real stale-handle boundary` | `TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationPoseSink::[[nodiscard]] virtual foundation::Result<void> Publish(std::shared_ptr<const PoseBuffer> pose)=0;` | `REVIEWED / scanner false positive for stale identity` | `TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::ISkeletonRegistry::[[nodiscard]] virtual foundation::Result<void> Freeze()=0;` | `REVIEWED / scanner false positive for stale identity` | `TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationEventBuffer::virtual void Clear()=0;` | `REVIEWED / scanner false positive for stale identity` | `TestValidationFailures; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Pause(AnimatorHandle handle)=0;` | `REVIEWED / real stale-handle boundary` | `TestPoseQueriesRejectStaleHandle; TestDeepFreezeAnimationContracts` |

## Lifecycle candidate review

Generated candidate count: 4.

| Candidate | Decision | Evidence |
| --- | --- | --- |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Pause(AnimatorHandle handle)=0;` | `REVIEWED / real lifecycle transition` | `TestStateTransitionsAndEvents; TestPauseRejectsStoppedAndFinished; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::ISkeletonRegistry::[[nodiscard]] virtual foundation::Result<void> Freeze()=0;` | `REVIEWED / real lifecycle transition` | `TestStateTransitionsAndEvents; TestPauseRejectsStoppedAndFinished; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationRuntime::[[nodiscard]] virtual foundation::Result<void> Stop(AnimatorHandle handle)=0;` | `REVIEWED / real lifecycle transition` | `TestStateTransitionsAndEvents; TestPauseRejectsStoppedAndFinished; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationClipRegistry::[[nodiscard]] virtual foundation::Result<void> Freeze()=0;` | `REVIEWED / real lifecycle transition` | `TestStateTransitionsAndEvents; TestPauseRejectsStoppedAndFinished; TestDeepFreezeAnimationContracts` |

## External-boundary candidate review

Generated candidate count: 2.

| Candidate | Decision | Evidence |
| --- | --- | --- |
| `epidemic::runtime::animation::IAnimationPoseSink::virtual ~IAnimationPoseSink()=default;` | `REVIEWED / scanner candidate is destructor; actual callback boundary reviewed` | `TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |
| `epidemic::runtime::animation::IAnimationEvaluatorBackend::virtual ~IAnimationEvaluatorBackend()=default;` | `REVIEWED / scanner candidate is destructor; actual callback boundary reviewed` | `TestResourceSourceFailureAndPoseSinkPublication; TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` |

## LOCAL_READY criteria handoff

| Criterion | Proposed decision after MSVC qualification | Module-specific rationale |
| --- | --- | --- |
| `ARCH-RESPONSIBILITY` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `ARCH-SINGLE-OWNER` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `ARCH-DIRECT-DEPS` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `ARCH-PORTS` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `API-CLASSIFIED` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `API-PRECONDITIONS` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `API-SUCCESS` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `API-FAILURE` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `API-OVERLOADS` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `API-INVALID` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `STATE-PRIMARY` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `STATE-INDEXES` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `STATE-COUNTERS` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `STATE-NO-FALSE-PUBLISH` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `STATE-NOOP` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `LIFE-ALLOWED` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `LIFE-FORBIDDEN` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `LIFE-SHUTDOWN` | `N/A` | Animation has no local Shutdown API in Goal 3. |
| `LIFE-RETRY-CLEANUP` | `N/A` | Animation owns no fallible external cleanup resource. |
| `ATOMIC-SINGLE` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `ATOMIC-MULTI` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `ATOMIC-EXTERNAL` | `PASS` | Pose evaluator/sink failures and exceptions preserve pre-state; sink publication precedes no-throw live commit. |
| `ATOMIC-RECONCILE` | `N/A` | Animation sink contract defines failure as not externally committed, so no post-success reconciliation token is owned locally. |
| `PERSIST-SNAPSHOT` | `N/A` | No local persistence checkpoint/restore contract exists in Goal 3; runtime state is transient and observational snapshots do not claim durability. |
| `PERSIST-VALIDATE` | `N/A` | No local persistence checkpoint/restore contract exists in Goal 3; runtime state is transient and observational snapshots do not claim durability. |
| `PERSIST-CANDIDATE` | `N/A` | No local persistence checkpoint/restore contract exists in Goal 3; runtime state is transient and observational snapshots do not claim durability. |
| `PERSIST-FAILURE` | `N/A` | No local persistence checkpoint/restore contract exists in Goal 3; runtime state is transient and observational snapshots do not claim durability. |
| `PERSIST-CONTINUITY` | `N/A` | No local persistence checkpoint/restore contract exists in Goal 3; runtime state is transient and observational snapshots do not claim durability. |
| `TEST-HAPPY` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `TEST-INVALID` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `TEST-DUPLICATE` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `TEST-STALE` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `TEST-EMPTY` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `TEST-BOUNDARY` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `TEST-WRONG-LIFECYCLE` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `TEST-CALLBACK-FAILURE` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `TEST-REGRESSION` | `PASS` | G3-ANIM-001, G3-ANIM-002, M3C-ANIM-001 and M3C-ANIM-002 remain covered. |

## Defect evidence handoff

| ID | Verified defect / contract | Fix present in this delta or baseline | Regression | CTest target |
| --- | --- | --- | --- | --- |
| `G3-ANIM-001` | Live animator state previously committed before confirmed pose publication. | Existing candidate-before-sink/no-throw commit retained. | `animation_tests.cpp::TestPoseSinkFailurePropagates; TestDeepFreezeAnimationContracts` | `EpidemicRuntimeAnimationTests` |
| `G3-ANIM-002` | Post-commit semantic event publication could allocate/lose events. | Existing preallocated bounded event storage retained. | `animation_tests.cpp::TestEventCapacityDropsOldest; TestZeroEventCapacityUsesDefaultBound; TestDeepFreezeAnimationContracts` | `EpidemicRuntimeAnimationTests` |
| `M3C-ANIM-001` | `long double` scaling can equal binary64 on MSVC and allow out-of-range `int64_t` conversion at the exact upper boundary. | Exact binary64 decomposition plus portable checked wide-integer scaling; product fraction and carried remainder are combined exactly on a `2^-1074` fixed-point grid before round-to-nearest-even storage. | `animation_tests.cpp::TestPortablePlaybackScalingBoundaries` | `EpidemicRuntimeAnimationTests` |
| `M3C-ANIM-002` | Rounding the exact fractional grid to one `double` after every Tick discarded residual state needed by a later legitimate carry. | `AnimatorRecord` now retains the exact private fraction grid across staged ticks; the `double` field is a derived mirror only, and reset paths clear both forms. | `animation_tests.cpp::TestStatefulPlaybackRemainderPartitionAndFailureAtomicity` | `EpidemicRuntimeAnimationTests` |

## Qualification recorded by Block G

GCC Debug/Release and Clang Debug/Release pass with warnings-as-errors. Clang ASan + UBSan + float-cast-overflow pass. Independent exact binary-rational oracle: 1,000,000 mixed boundary/random cases with no validity, whole-microsecond or bitwise fractional-remainder mismatch. Official MSVC Debug/Release remains pending in this Linux environment.
