# Block H evidence fragment

Исходный snapshot: `Epidemic 21-09-2026-3(2).zip`.

Область: `EngineRuntime/Support/**`, Runtime Architecture/Integration/Regression suites и test-only `EngineFramework/DevelopmentInfrastructure/Tests/gameplay_integration_tests.cpp`. Shared freeze registries и production GameFramework этим блоком не изменяются.

Статус блока: code review и non-MSVC qualification completed. Финальный `LOCAL_READY` для Support требует интегрированный MSVC Debug/Release pass после merge остальных блоков.

## H1. G3-SUP defect evidence

| Defect | Проверенный contract | Regression / target |
|---|---|---|
| G3-SUP-001 | Scene projection successful prefix больше не принадлежит retry queue; late failure сохраняет только failed suffix. | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestSceneProjectionPrefixRetry`, `EpidemicRuntimeSupportTests` |
| G3-SUP-002 | Coordinator держит accepted frame prefix в `PendingCoordinatorFrame`; другой input до reconciliation rejected. | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestCoordinatorAcceptedPrefixRetry`, `EpidemicRuntimeSupportTests` |
| G3-SUP-003 | Event sink failure/throw-before-commit не очищает source events; retry публикует retained batch. | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestReferenceEventSinkAtomicityAndCoordinatorContainment`, `EngineRuntime/Tests/Integration/integration_tests.cpp::TestEventPublishFailureRetainsSourceEvents` |
| G3-SUP-004 | Animation/Audio resource acquisition не имеет post-acquire publication gap; local owner готов до внешнего acquisition/transfer. | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationResourceLeasePublication`, `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAudioResourceLeaseTransferAndDestructorRetry` |
| G3-SUP-005 | Audio wrapper destruction не теряет failed release; retry ownership остаётся в lease state до `Shutdown()`. | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAudioResourceLeaseTransferAndDestructorRetry` |
| G3-SUP-006 | Reference audio voice ID commit происходит только после successful publication. | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestReferenceAudioVoiceIdCommit` |
| G3-SUP-007 | Pose owner/animator indexes публикуются одной strong-guarantee transaction. | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationPoseAtomicPublication` |

## H2. Preparation, commit, composition и shutdown

Дополнительно в этом блоке добавлены два regression tests: `TestIndividualRegistrationFailuresAreAtomic()` проверяет failure-before-publication для public `RegisterXxx` helpers и сохранение authoritative RuntimeFoundation при duplicate registration; `TestPreparedCommitDuplicateIsAtomic()` проверяет duplicate aggregate commit и сохранение уже зарегистрированного `EngineRuntimeServices` instance.

Повторный audit выявил `M3C-SUP-001`: `RegisterRenderer()` проверял duplicate service только в финальном `RegisterShared()`, уже после `EnsureMainView()`. Повторная регистрация поэтому создавала новый Scene node и меняла Scene revision, хотя сама регистрация завершалась ошибкой. Fix выполняет duplicate preflight до любых Scene mutations. Regression находится в `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration` и проверяет error code, сохранение authoritative Renderer service и неизменность Scene revision.

Остальные H2 свойства подтверждаются существующими tests: `TestProductionPreflightIsAtomic`, `TestAtomicDefaultCompositionAndTypedOwnership`, `TestPartialCompositeOverridesRejected`, `TestCoordinatorRejectsInvalidInputBeforeMutation`, `TestCoordinatorAcceptedPrefixRetry`, `TestShutdownRetrySkipsCompletedPrefix`, `TestFullTickAndTerminalShutdown` и integration `TestPhaseFailureDoesNotStopLaterPhases`/`TestCoordinatorShutdownRetriesAdapterCleanup`.

## H3. GameplayIntegration timeout blocker

M05 сохраняет один полный `AbilityService` activation/completion как canonical lifecycle proof. Retention stress затем выполняет ровно 4097 deliveries через `AbilityTimeAdapter::RestoreCheckpoint() -> AbilityOutputDeliveryCoordinator::DeliverPendingOutputs() -> AbilityEffectsDispatcher`, используя valid synthetic execution/schedule identities, которые моделируют уже purged terminal executions. После каждой итерации проверяются successful effect result, empty pending outbox и empty delivery ledger после safe prune.

Production GameFramework не менялся. Отдельный performance debt остаётся на будущий Framework pass: `AbilityService::Record()` использует bounded `std::vector` journal и `erase(begin())` при насыщении. Goal 3 больше не умножает этот cost 4097 полными lifecycles.

## H4. Runtime-wide gate anchors

Architecture: `EpidemicRuntimeArchitectureTests`. Integration: `EpidemicRuntimeIntegrationTests`. Regression: `EpidemicRuntimeRegressionTests`. Support reference smoke и production preflight negative smoke остаются в `EpidemicRuntimeSupportTests`/Regression. Cross-major production dependency ради test setup не добавлена.

## H5. Public callable anchor proposals

Ниже 53/53 callables текущего generated inventory. Stable IDs должны быть пересчитаны serial integrator после merge; поэтому authoritative key для переноса: `(scope, signature)`.

| Current ID | Callable | Contract anchor proposal | Test/audit anchor proposal |
|---|---|---|---|
| `6b5840f4c5ffe0c2` | `epidemic::runtime::IChunkStreamingManifestSource::virtual ~IChunkStreamingManifestSource()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Tests/Integration/integration_tests.cpp::TestStreamingUsesResourcesAndPersistence` |
| `2b5348b5afb48449` | `epidemic::runtime::IChunkStreamingManifestSource::[[nodiscard]] virtual foundation::Result<ChunkStreamingManifest> GetManifest(ChunkId chunk)const=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Tests/Integration/integration_tests.cpp::TestStreamingUsesResourcesAndPersistence` |
| `6a4f1539db7fc574` | `epidemic::runtime::IStreamingPreparedChunkDataQuery::virtual ~IStreamingPreparedChunkDataQuery()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestStreamingPrepareDataAndCleanupRetry` |
| `90f6c7a1c9d69ce7` | `epidemic::runtime::IStreamingPreparedChunkDataQuery::[[nodiscard]] virtual foundation::Result<PreparedChunkDataSnapshot> GetPreparedData(ChunkId chunk)const=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestStreamingPrepareDataAndCleanupRetry` |
| `4c24985bd7f295a4` | `epidemic::runtime::IAnimationResourceMapper::virtual ~IAnimationResourceMapper()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationResourceLeasePublication` |
| `2a49cc19d233f465` | `epidemic::runtime::IAnimationResourceMapper::[[nodiscard]] virtual foundation::Result<ResourceRequest> ResolveSkeleton(animation::SkeletonId id)const=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationResourceLeasePublication` |
| `747fa1c7bd59ac6d` | `epidemic::runtime::IAnimationResourceMapper::[[nodiscard]] virtual foundation::Result<ResourceRequest> ResolveClip(animation::AnimationClipId id)const=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationResourceLeasePublication` |
| `8c560a0e919f7a23` | `epidemic::runtime::IAnimationSkeletonResourcePayload::virtual ~IAnimationSkeletonResourcePayload()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationResourceLeasePublication` |
| `a6efde0e133fb75c` | `epidemic::runtime::IAnimationSkeletonResourcePayload::[[nodiscard]] virtual const animation::SkeletonDesc&GetSkeleton()const noexcept=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationResourceLeasePublication` |
| `f73909491d333ba3` | `epidemic::runtime::IAnimationClipResourcePayload::virtual ~IAnimationClipResourcePayload()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationResourceLeasePublication` |
| `2cfc8a66f35acc57` | `epidemic::runtime::IAnimationClipResourcePayload::[[nodiscard]] virtual const animation::AnimationClipDesc&GetClip()const noexcept=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationResourceLeasePublication` |
| `ff322fb5676c1831` | `epidemic::runtime::IAudioResourceMapper::virtual ~IAudioResourceMapper()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAudioResourceLeaseTransferAndDestructorRetry` |
| `c145d82da854391e` | `epidemic::runtime::IAudioResourceMapper::[[nodiscard]] virtual foundation::Result<ResourceRequest> ResolveSound(audio::SoundId id)const=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAudioResourceLeaseTransferAndDestructorRetry` |
| `8259739c579e04e6` | `epidemic::runtime::ISceneProjectionQueue::virtual ~ISceneProjectionQueue()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestSceneProjectionQueue + TestSceneProjectionPrefixRetry` |
| `fcf8de52d19e7f2e` | `epidemic::runtime::ISceneProjectionQueue::[[nodiscard]] virtual foundation::Result<std::size_t> Flush()=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestSceneProjectionQueue + TestSceneProjectionPrefixRetry` |
| `91751f871275bd78` | `epidemic::runtime::ISceneProjectionQueue::[[nodiscard]] virtual std::size_t PendingCount()const noexcept=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestSceneProjectionQueue + TestSceneProjectionPrefixRetry` |
| `d79f0ddf5e6b698c` | `epidemic::runtime::ISceneProjectionQueue::[[nodiscard]] virtual foundation::Result<void> DiscardPending()=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestSceneProjectionQueue + TestSceneProjectionPrefixRetry` |
| `7260b1f7ae9647d0` | `epidemic::runtime::IRuntimePoseCache::virtual ~IRuntimePoseCache()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationPoseAtomicPublication` |
| `1b500bab1a319ae4` | `epidemic::runtime::IRuntimePoseCache::[[nodiscard]] virtual foundation::Result<std::shared_ptr<const animation::PoseBuffer>> GetPose(animation::AnimatorHandle animator)const=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationPoseAtomicPublication` |
| `6066fd435d716e54` | `epidemic::runtime::IRuntimePoseCache::[[nodiscard]] virtual std::size_t PoseCount()const noexcept=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAnimationPoseAtomicPublication` |
| `a8c63aea82c29f38` | `epidemic::runtime::IRuntimeEventSink::virtual ~IRuntimeEventSink()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestReferenceEventSinkAtomicityAndCoordinatorContainment` |
| `8984ddafa612fa0f` | `epidemic::runtime::IRuntimeEventSink::[[nodiscard]] virtual foundation::Result<void> Publish(const RuntimeFrameEvents&events)=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestReferenceEventSinkAtomicityAndCoordinatorContainment` |
| `e591575dffc12010` | `epidemic::runtime::IRuntimeAdapterLifecycle::virtual ~IRuntimeAdapterLifecycle()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix` |
| `0134ab68f91e7eb1` | `epidemic::runtime::IRuntimeAdapterLifecycle::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix` |
| `d47b3d5654e621cd` | `epidemic::runtime::IReferenceSimulationCommitLog::virtual ~IReferenceSimulationCommitLog()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/src/runtime_support.cpp::RuntimeSimulationCommitTarget::CommittedBatches + EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAtomicDefaultCompositionAndTypedOwnership` |
| `2754570b3dcc48eb` | `epidemic::runtime::IReferenceSimulationCommitLog::[[nodiscard]] virtual std::span<const simulation::SimulationProposalBatch> CommittedBatches()const noexcept=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/src/runtime_support.cpp::RuntimeSimulationCommitTarget::CommittedBatches + EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAtomicDefaultCompositionAndTypedOwnership` |
| `1b6fe70216cd3057` | `epidemic::runtime::IEngineRuntimeCoordinator::virtual ~IEngineRuntimeCoordinator()=default;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestFullTickAndTerminalShutdown` |
| `5b32535b0e941680` | `epidemic::runtime::IEngineRuntimeCoordinator::[[nodiscard]] virtual foundation::Result<RuntimeTickResult> Tick(const RuntimeFrameInput&input)=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestCoordinatorAcceptedPrefixRetry + TestFullTickAndTerminalShutdown` |
| `4ed18c9f6828f3b0` | `epidemic::runtime::IEngineRuntimeCoordinator::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix + TestFullTickAndTerminalShutdown` |
| `bdf9b845b720dcd4` | `epidemic::runtime::IEngineRuntimeCoordinator::[[nodiscard]] virtual bool IsShutdownStarted()const noexcept=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix + TestFullTickAndTerminalShutdown` |
| `e775c1995499b44c` | `epidemic::runtime::IEngineRuntimeCoordinator::[[nodiscard]] virtual bool IsShutdownComplete()const noexcept=0;` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix + TestFullTickAndTerminalShutdown` |
| `b6f40ab0f4c8f422` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterRuntimeFoundation(core::Application&app);` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `06c1d6278cb49e80` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterAssets(core::Application&app,const AssetsOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `b8e33480e99de1d4` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterSerialization(core::Application&app,const SerializationOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `4427e33a78519b77` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterResources(core::Application&app,const ResourceOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `cd4202a31e5dd9dc` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterPersistence(core::Application&app,const PersistenceOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `a8f6f2272561bf01` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterTime(core::Application&app,const TimeOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `7eddf8229f9429ca` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterEnvironment(core::Application&app,const EnvironmentOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `c1a89dd2d8605024` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterScene(core::Application&app,const SceneOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `3767431a277b2bd9` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterWorld(core::Application&app,const WorldOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `37b0f01c0c466d14` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterStreaming(core::Application&app);` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `e744db09d49dce5c` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterSimulation(core::Application&app,const simulation::SimulationOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `b347dc31d54a6a33` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterNavigation(core::Application&app,navigation::NavigationOptions options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `8d18b5e0ad6f96c8` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterAnimation(core::Application&app,animation::AnimationOptions options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `36f01cba7c3052bc` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterPhysics(core::Application&app);` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `52a1466cb4ec32c5` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterAudio(core::Application&app,audio::AudioOptions options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `52692e1a7f79e3e6` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterRenderer(core::Application&app,const renderer::RendererOptions&options={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` (`M3C-SUP-001` duplicate preflight/Scene atomicity) |
| `f30a458a3baeae7c` | `epidemic::runtime::[[nodiscard]] foundation::Result<PreparedEngineRuntime> PrepareEngineRuntime(const EngineRuntimeOptions&options={},EngineRuntimeDependencies dependencies={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestProductionPreflightIsAtomic + TestPartialCompositeOverridesRejected` |
| `95b117ed90bc02e0` | `epidemic::runtime::[[nodiscard]] foundation::Result<EngineRuntimeServices> CommitPreparedRuntime(core::Application&app,PreparedEngineRuntime prepared);` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestPreparedCommitDuplicateIsAtomic` |
| `8b7653e1a2cc3c69` | `epidemic::runtime::[[nodiscard]] foundation::Result<EngineRuntimeServices> RegisterDefaultEngineRuntime(core::Application&app,const EngineRuntimeOptions&options={},EngineRuntimeDependencies dependencies={});` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAtomicDefaultCompositionAndTypedOwnership + TestProductionPreflightIsAtomic` |
| `67747360d1cf839a` | `epidemic::runtime::[[nodiscard]] std::vector<RuntimeAdapterKind> GetAllowedRuntimeAdapters();` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Tests/Architecture/architecture_tests.cpp::TestSupportProfileContracts` |
| `44bb691f3f3e6f68` | `epidemic::runtime::[[nodiscard]] std::vector<RuntimeUpdateStep> GetRuntimeUpdateOrder();` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestFullTickAndTerminalShutdown` |
| `0fb2f318786071b9` | `epidemic::runtime::[[nodiscard]] std::vector<RuntimeShutdownStep> GetRuntimeShutdownOrder();` | `docs/EngineRuntime/modules/support.md` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestFullTickAndTerminalShutdown` |

## Coverage child review decisions

### Mutator obligations

Все 25 Support MUTATOR/LIFECYCLE callables, дающие 100 generated obligations (`preconditions/success/noop/failure`), reviewed по production source и перечисленным regressions. Shared JSON здесь намеренно не меняется.

| Callable ID | Decision | Evidence |
|---|---|---|
| `0134ab68f91e7eb1` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix` |
| `06c1d6278cb49e80` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `36f01cba7c3052bc` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `3767431a277b2bd9` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `37b0f01c0c466d14` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `4427e33a78519b77` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `4ed18c9f6828f3b0` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix + TestFullTickAndTerminalShutdown` |
| `52692e1a7f79e3e6` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `52a1466cb4ec32c5` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `5b32535b0e941680` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestCoordinatorAcceptedPrefixRetry + TestFullTickAndTerminalShutdown` |
| `7eddf8229f9429ca` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `8984ddafa612fa0f` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestReferenceEventSinkAtomicityAndCoordinatorContainment` |
| `8b7653e1a2cc3c69` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestAtomicDefaultCompositionAndTypedOwnership + TestProductionPreflightIsAtomic` |
| `8d18b5e0ad6f96c8` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `95b117ed90bc02e0` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestPreparedCommitDuplicateIsAtomic` |
| `a8f6f2272561bf01` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `b347dc31d54a6a33` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `b6f40ab0f4c8f422` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `b8e33480e99de1d4` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `c1a89dd2d8605024` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `cd4202a31e5dd9dc` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `d79f0ddf5e6b698c` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestSceneProjectionQueue + TestSceneProjectionPrefixRetry` |
| `e744db09d49dce5c` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestIndividualRegistration + TestIndividualRegistrationFailuresAreAtomic` |
| `f30a458a3baeae7c` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestProductionPreflightIsAtomic + TestPartialCompositeOverridesRejected` |
| `fcf8de52d19e7f2e` | `preconditions/success/noop/failure = REVIEWED proposal` | `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestSceneProjectionQueue + TestSceneProjectionPrefixRetry` |

### Stale-identity candidates

Все 20 generated Support stale-identity candidates являются scanner false positives для Support: соответствующие signatures не принимают generation-bearing identity/handle, а регистрируют service bundles, публикуют event batch, очищают projection queue либо выполняют lifecycle cleanup. Stale-handle contracts остаются у owning Runtime majors/adapters и не должны дублироваться в Support.

| Candidate | Callable | Decision |
|---|---|---|
| `3c3428bcc8190189` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterResources(core::Application&app,const ResourceOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `470c1a40e445c14f` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterRenderer(core::Application&app,const renderer::RendererOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `4cb69721a14dcdf2` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterAnimation(core::Application&app,animation::AnimationOptions options={});` | `REVIEWED false positive / N/A stale identity` |
| `4ddbd1e08f867b0d` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterAudio(core::Application&app,audio::AudioOptions options={});` | `REVIEWED false positive / N/A stale identity` |
| `57454fdcb71766ba` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterPersistence(core::Application&app,const PersistenceOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `5776ea841a721883` | `epidemic::runtime::IEngineRuntimeCoordinator::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `REVIEWED false positive / N/A stale identity` |
| `5f439149a2ba7ea0` | `epidemic::runtime::IRuntimeAdapterLifecycle::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `REVIEWED false positive / N/A stale identity` |
| `76a5b2ed6bb6c311` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterAssets(core::Application&app,const AssetsOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `939504eba900de3a` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterScene(core::Application&app,const SceneOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `9bee24d3c1f995db` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterStreaming(core::Application&app);` | `REVIEWED false positive / N/A stale identity` |
| `a1a9311646751c71` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterTime(core::Application&app,const TimeOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `b9d232288e49d426` | `epidemic::runtime::IRuntimeEventSink::[[nodiscard]] virtual foundation::Result<void> Publish(const RuntimeFrameEvents&events)=0;` | `REVIEWED false positive / N/A stale identity` |
| `c9b4bc0ddf80117a` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterSimulation(core::Application&app,const simulation::SimulationOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `d47c2444aa8b08b5` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterWorld(core::Application&app,const WorldOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `e32dbb963d3149e0` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterPhysics(core::Application&app);` | `REVIEWED false positive / N/A stale identity` |
| `e4ed4edd86b61eef` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterEnvironment(core::Application&app,const EnvironmentOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `e6d8b531e2ee4e4b` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterRuntimeFoundation(core::Application&app);` | `REVIEWED false positive / N/A stale identity` |
| `ee43a7fec2326e1f` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterNavigation(core::Application&app,navigation::NavigationOptions options={});` | `REVIEWED false positive / N/A stale identity` |
| `f2a04a2d3561bd83` | `epidemic::runtime::[[nodiscard]] foundation::Result<void> RegisterSerialization(core::Application&app,const SerializationOptions&options={});` | `REVIEWED false positive / N/A stale identity` |
| `f5a740533614e165` | `epidemic::runtime::ISceneProjectionQueue::[[nodiscard]] virtual foundation::Result<void> DiscardPending()=0;` | `REVIEWED false positive / N/A stale identity` |

### Lifecycle candidates

| Candidate | Callable | Decision |
|---|---|---|
| `999111583ea8fe73` | `epidemic::runtime::IRuntimeAdapterLifecycle::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `REVIEWED real lifecycle`; `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix` |
| `b272afe58d4e1ddc` | `epidemic::runtime::IEngineRuntimeCoordinator::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `REVIEWED real lifecycle`; `EngineRuntime/Support/tests/runtime_support_tests.cpp::TestShutdownRetrySkipsCompletedPrefix + TestFullTickAndTerminalShutdown` |

### External-boundary candidate

Generated candidate `3b2850407d6cab5c` points to `IRuntimeEventSink` destructor and is a scanner false positive. The real external call boundary is `IRuntimeEventSink::Publish`, which is reviewed by G3-SUP-003 and both Support/Integration event retry regressions.

## Dossier field decisions

| Field | Proposal | Reviewed rationale |
|---|---|---|
| Responsibility | REVIEWED | Official Runtime composition/cross-major adapter layer; no duplicate gameplay/world ownership. |
| Authoritative state | REVIEWED | Owns composition root, adapter ownership, coordinator pending-frame/shutdown bookkeeping and reference-adapter local ownership only. |
| Derived/cache/index state | REVIEWED | Pose dual indexes, prepared streaming data, lease tables and reference event batch are derived/integration state with explicit strong-guarantee publication. |
| Dependency list | REVIEWED | Support is the allowed cross-major Runtime composition layer and links Runtime majors plus Core/Foundation; majors do not link back to Support. |
| External ports/callbacks/providers/backends | REVIEWED | Production dependencies and event/resource/backend callbacks are explicit typed ports; Result/exception containment reviewed at adapter/coordinator boundaries. |
| Hard limits, budgets and complexity bounds | REVIEWED | Fixed 12-step update/shutdown sequences, coordinator reserved bookkeeping, per-major budgets and bounded adapter queues/ledgers are explicit. |
| ID spaces, generations, revisions and cursors | REVIEWED | Support does not create a second global identity authority; local reference IDs/cursors commit only with authoritative publication. |
| Local invariants | REVIEWED | One adapter object per advertised role set, accepted-prefix ownership, no lost lease/projection/event ownership, terminal coordinator semantics. |
| Persistent and transient state | REVIEWED | Support has no save-authoritative persistent state; composition/adapters/pending frame/shutdown cursors are transient runtime state. |
| Public headers and types | REVIEWED | Public Support surface is `runtime_support.h`; no Framework type dependency is introduced into EngineRuntime. |
| Public mutation API | REVIEWED | Register/Prepare/Commit/Tick/Shutdown and typed adapter mutations reviewed for precondition, success and failure atomicity. |
| Read/query API for invariants | REVIEWED | Orders, shutdown flags, pose/prepared-data/projection queries return explicit observations without becoming second authoritative state. |
| Snapshot/restore contract | REVIEWED | N/A for module-level persistence snapshot/restore; Support exposes detached integration observations and private retry state instead. |
| State machines | REVIEWED | Coordinator active -> shutdown-started -> complete, pending-frame retry, adapter cleanup and projection/lease ownership transitions reviewed. |
| Threading contract | REVIEWED | No public multi-thread mutation guarantee is claimed; composition/coordinator mutation is owner-thread orchestration and external callbacks are contained. |

## LOCAL_READY criteria proposals

| Criterion | Proposal | Rationale / evidence |
|---|---|---|
| `ARCH-RESPONSIBILITY` | `PASS` | Generated ownership matrix plus Support module contract. |
| `ARCH-SINGLE-OWNER` | `PASS` | Support owns wiring/retry bookkeeping only; majors remain authoritative owners. |
| `ARCH-DIRECT-DEPS` | `PASS` | Architecture gate and Runtime major CMake rules; no peer-major dependency introduced by this delta. |
| `ARCH-PORTS` | `PASS` | All cross-major links are typed adapters/ports in `runtime_support.h`. |
| `API-CLASSIFIED` | `PASS` | 53/53 Support callables are classified `REVIEWED` in current coverage inventory. |
| `API-PRECONDITIONS` | `PASS` | Register/Prepare/Commit/Tick preconditions covered by failure atomicity, production preflight and invalid-frame tests. |
| `API-SUCCESS` | `PASS` | Reference composition, individual registration and full tick/shutdown success paths covered. |
| `API-FAILURE` | `PASS` | Injected adapter/coordinator/event/lease failures preserve explicit ownership and return controlled errors. |
| `API-OVERLOADS` | `PASS` | Public Support surface reviewed; no ambiguous overload family with divergent mutation semantics. |
| `API-INVALID` | `PASS` | Missing dependencies, partial composite overrides and invalid frame/budget inputs reject before mutation. |
| `STATE-PRIMARY` | `PASS` | Composition root and coordinator/adapters have explicit single owners. |
| `STATE-INDEXES` | `PASS` | Pose indexes and adapter lookup state publish atomically; G3-SUP-007 regression. |
| `STATE-COUNTERS` | `PASS` | Coordinator cursors, revisions and reference voice IDs advance only with accepted publication. |
| `STATE-NO-FALSE-PUBLISH` | `PASS` | G3-SUP-001..007 cover projection/event/lease/voice/pose publication gaps. |
| `STATE-NOOP` | `PASS` | Repeated shutdown after completion and duplicate-safe query paths are defined; duplicate registrations reject without mutation. |
| `LIFE-ALLOWED` | `PASS` | Prepare/commit/tick/shutdown legal sequences exercised by Support tests. |
| `LIFE-FORBIDDEN` | `PASS` | Tick after shutdown and mismatched pending-frame retry are rejected. |
| `LIFE-SHUTDOWN` | `PASS` | Terminal flag set on first Shutdown and full order verified. |
| `LIFE-RETRY-CLEANUP` | `PASS` | Failed cleanup retains ownership and retry skips completed prefix. |
| `ATOMIC-SINGLE` | `PASS` | Public registration and adapter single-operation publication use strong-guarantee paths; `M3C-SUP-001` prevents duplicate Renderer registration from mutating Scene before rejection. |
| `ATOMIC-MULTI` | `PASS` | Aggregate commit, pose dual-index publication, streaming/lease multi-owner transitions covered. |
| `ATOMIC-EXTERNAL` | `PASS` | External callback success/failure ownership is explicit for events, resource manager, projection and backend adapters. |
| `ATOMIC-RECONCILE` | `PASS` | Coordinator pending-frame and shutdown retry reconcile accepted prefix without replay. |
| `PERSIST-SNAPSHOT` | `N/A` | Support is not persistence-authoritative and exposes no module persistence snapshot contract. |
| `PERSIST-VALIDATE` | `N/A` | No Support persistence restore input exists. |
| `PERSIST-CANDIDATE` | `N/A` | No Support persistence restore candidate publication exists. |
| `PERSIST-FAILURE` | `N/A` | Persistence failure semantics belong to EngineRuntime/Persistence; Support only orchestrates typed Results. |
| `PERSIST-CONTINUITY` | `N/A` | No Support-owned durable save state to continue across restore. |
| `TEST-HAPPY` | `PASS` | Reference composition, integrations, full tick and shutdown pass. |
| `TEST-INVALID` | `PASS` | Production preflight, partial overrides, invalid frame/budget and missing registration dependencies covered. |
| `TEST-DUPLICATE` | `PASS` | Duplicate aggregate and RuntimeFoundation/Renderer registration preserve previous authoritative service. |
| `TEST-STALE` | `N/A` | Generated Support stale-identity candidates are reviewed scanner false positives; no generation-bearing public handle consumer is owned here. |
| `TEST-EMPTY` | `PASS` | Default composition/tick and empty adapter queues execute successfully. |
| `TEST-BOUNDARY` | `PASS` | Accepted-prefix faults, external callback faults, cleanup faults and H3 4096->4097 retention boundary are covered. |
| `TEST-WRONG-LIFECYCLE` | `PASS` | Tick after shutdown and registration before prerequisites are rejected. |
| `TEST-CALLBACK-FAILURE` | `PASS` | Event sink, resource manager, projection and cleanup Result/exception boundaries have regressions. |
| `TEST-REGRESSION` | `PASS` | G3-SUP-001..007 permanent regressions plus H3 bounded retention regression are present. |

## Qualification notes

Expected final Windows gate remains: `EpidemicRuntimeSupportTests` Debug/Release, Runtime Architecture/Integration/Regression Debug/Release, and full-profile `EpidemicGameFrameworkGameplayIntegrationTests` with the standard 300 s timeout. This evidence fragment must not be used to claim final MSVC qualification before that run.
