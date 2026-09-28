# Goal 3 Block G evidence fragment: Renderer

Date: 2026-09-21. This is module-local handoff evidence. Shared generated manifests, anchor registry and `local_ready_ledger.json` are intentionally unchanged. Final admission remains conditional on integrated MSVC Debug/Release qualification after merge.

## Public signature anchor proposals

The committed generated inventory contains 49 Renderer public callables. The proposals are signature-keyed so the serial integrator can bind regenerated stable IDs after all parallel deltas are merged.

| Public declaration | Proposed contract anchor | Proposed test/audit anchor |
| --- | --- | --- |
| `epidemic::runtime::renderer::IRenderPoseSource::virtual ~IRenderPoseSource()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestPoseReadIsStagedBeforeBeginAndTransformCommitIsAtomic; TestPoseExceptionAndInvalidPayloadAreRejectedBeforeBegin` |
| `epidemic::runtime::renderer::IRenderPoseSource::[[nodiscard]] virtual foundation::Result<std::shared_ptr<const RenderPoseBuffer>> GetPose(RuntimeObjectId owner)const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestPoseReadIsStagedBeforeBeginAndTransformCommitIsAtomic; TestPoseExceptionAndInvalidPayloadAreRejectedBeforeBegin` |
| `epidemic::runtime::renderer::IRenderResourceBridge::virtual ~IRenderResourceBridge()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestResourceBridgeExceptionsPreserveOwnedRetryState; TestDeferredDestroyReleaseFailureIsRetryablePrefixProgress` |
| `epidemic::runtime::renderer::IRenderResourceBridge::[[nodiscard]] virtual foundation::Result<void> AcquirePayloads(ResourceId mesh,ResourceId material)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestResourceBridgeExceptionsPreserveOwnedRetryState; TestDeferredDestroyReleaseFailureIsRetryablePrefixProgress` |
| `epidemic::runtime::renderer::IRenderResourceBridge::[[nodiscard]] virtual foundation::Result<void> ReleasePayloads(ResourceId mesh,ResourceId material)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestResourceBridgeExceptionsPreserveOwnedRetryState; TestDeferredDestroyReleaseFailureIsRetryablePrefixProgress` |
| `epidemic::runtime::renderer::IRenderResourceBridge::[[nodiscard]] virtual foundation::Result<RenderResourcePayloads> GetPayloads(ResourceId mesh,ResourceId material)const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestResourceBridgeExceptionsPreserveOwnedRetryState; TestDeferredDestroyReleaseFailureIsRetryablePrefixProgress` |
| `epidemic::runtime::renderer::IRenderSceneSource::virtual ~IRenderSceneSource()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::IRenderSceneSource::[[nodiscard]] virtual foundation::Result<RenderTransformSnapshot> GetTransformSnapshot(RenderTransformId node)const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::IRenderCommandSink::virtual ~IRenderCommandSink()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRenderFrameAbortOnSubmitFailure; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestBeginAndEndExceptionFailureMatrix` |
| `epidemic::runtime::renderer::IRenderCommandSink::[[nodiscard]] virtual foundation::Result<void> BeginFrame(const RenderFrameContext&context)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRenderFrameAbortOnSubmitFailure; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestBeginAndEndExceptionFailureMatrix` |
| `epidemic::runtime::renderer::IRenderCommandSink::[[nodiscard]] virtual foundation::Result<void> SubmitProxy(const RenderProxySubmission&submission)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRenderFrameAbortOnSubmitFailure; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestBeginAndEndExceptionFailureMatrix` |
| `epidemic::runtime::renderer::IRenderCommandSink::[[nodiscard]] virtual foundation::Result<void> EndFrame()=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRenderFrameAbortOnSubmitFailure; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestBeginAndEndExceptionFailureMatrix` |
| `epidemic::runtime::renderer::IRenderCommandSink::[[nodiscard]] virtual foundation::Result<void> AbortFrame()=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRenderFrameAbortOnSubmitFailure; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestBeginAndEndExceptionFailureMatrix` |
| `epidemic::runtime::renderer::IRenderScene::virtual ~IRenderScene()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<RenderProxyId> RegisterProxy(const RenderProxyDesc&desc)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> DestroyProxy(RenderProxyId id)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> FlushDeferredDestroys()=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> MarkTransformDirty(RenderProxyId id)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> MarkMaterialDirty(RenderProxyId id)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> SetProxyVisibility(RenderProxyId id,RenderProxyVisibility visibility)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual RenderProxyLifecycle GetProxyLifecycle(RenderProxyId id)const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual RenderProxyReadiness GetProxyReadiness(RenderProxyId id)const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual RenderProxyVisibility GetProxyVisibility(RenderProxyId id)const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual RenderProxyDirtyMask GetProxyDirtyFlags(RenderProxyId id)const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::RenderProxyId::[[nodiscard]] constexpr bool IsValid()const noexcept` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::RenderProxyId::[[nodiscard]] constexpr bool operator==(const RenderProxyId&)const noexcept=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::ViewId::[[nodiscard]] constexpr bool IsValid()const noexcept` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |
| `epidemic::runtime::renderer::ViewId::[[nodiscard]] constexpr bool operator==(const ViewId&)const noexcept=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |
| `epidemic::runtime::renderer::IRenderMeshResource::virtual ~IRenderMeshResource()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::IRenderMaterialResource::virtual ~IRenderMaterialResource()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::[[nodiscard]] constexpr RenderProxyDirtyMask ToRenderDirtyMask(RenderProxyDirtyFlags flag)noexcept` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::[[nodiscard]] constexpr bool HasRenderDirtyFlag(RenderProxyDirtyMask mask,RenderProxyDirtyFlags flag)noexcept` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::[[nodiscard]] constexpr RenderProxyDirtyMask AddRenderDirtyFlag(RenderProxyDirtyMask mask,RenderProxyDirtyFlags flag)noexcept` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `epidemic::runtime::renderer::[[nodiscard]] constexpr RenderProxyDirtyMask ClearRenderDirtyFlag(RenderProxyDirtyMask mask,RenderProxyDirtyFlags flag)noexcept` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRegisterProxyTracksSplitState; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame` |
| `std::hash::[[nodiscard]] size_t operator()(epidemic::runtime::renderer::RenderProxyId value)const noexcept` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `std::hash::[[nodiscard]] size_t operator()(epidemic::runtime::renderer::ViewId value)const noexcept` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |
| `epidemic::runtime::renderer::IRendererRuntime::virtual ~IRendererRuntime()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestRendererEnumValidationAndIdExhaustion; TestProxyDescriptorValidationIsPrePublication` |
| `epidemic::runtime::renderer::IRendererRuntime::[[nodiscard]] virtual foundation::Result<void> PrepareFrame()=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestFrameFlowEndsSubmittedAndClearsDirty; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestShutdownReconcilesOpenFrameBeforeResourceRelease` |
| `epidemic::runtime::renderer::IRendererRuntime::[[nodiscard]] virtual foundation::Result<void> RenderFrame()=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestFrameFlowEndsSubmittedAndClearsDirty; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestShutdownReconcilesOpenFrameBeforeResourceRelease` |
| `epidemic::runtime::renderer::IRendererRuntime::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestFrameFlowEndsSubmittedAndClearsDirty; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestShutdownReconcilesOpenFrameBeforeResourceRelease` |
| `epidemic::runtime::renderer::IRendererRuntime::[[nodiscard]] virtual RenderFrameState GetFrameState()const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestFrameFlowEndsSubmittedAndClearsDirty; TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestShutdownReconcilesOpenFrameBeforeResourceRelease` |
| `epidemic::runtime::renderer::[[nodiscard]] foundation::Result<RendererServices> CreateRendererServices(const RendererOptions&options,RendererDependencies dependencies={});` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestStrictAndMockFactories` |
| `epidemic::runtime::renderer::[[nodiscard]] foundation::Result<RendererServices> CreateMockRendererServices();` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestStrictAndMockFactories` |
| `epidemic::runtime::renderer::IViewSystem::virtual ~IViewSystem()=default;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |
| `epidemic::runtime::renderer::IViewSystem::[[nodiscard]] virtual foundation::Result<ViewId> CreateView(const ViewDesc&desc)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |
| `epidemic::runtime::renderer::IViewSystem::[[nodiscard]] virtual foundation::Result<void> DestroyView(ViewId view)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |
| `epidemic::runtime::renderer::IViewSystem::[[nodiscard]] virtual foundation::Result<void> SetMainView(ViewId view)=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |
| `epidemic::runtime::renderer::IViewSystem::[[nodiscard]] virtual ViewId GetMainView()const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |
| `epidemic::runtime::renderer::IViewSystem::[[nodiscard]] virtual ViewLifecycle GetViewLifecycle(ViewId view)const=0;` | `docs/EngineRuntime/modules/renderer.md::Контракты / Backend frame ownership / Identity, cleanup и shutdown` | `renderer_tests.cpp::TestViewLifecycleAndMainView; TestMainViewDestroyPendingAndMissingBehavior` |

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

The current generated coverage input contains 80 mutator obligations for Renderer. All generated `preconditions`, `success`, `failure` and `noop` records were reviewed by public mutator family against the module contract and the named regressions in the signature table. A scanner `noop` obligation is treated as defined behavior, not as a requirement that every mutator must succeed as a no-op. No shared coverage status is changed in this parallel delta.


## Stale-identity candidate review

Generated candidate count: 18.

| Candidate | Decision | Evidence |
| --- | --- | --- |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> FlushDeferredDestroys()=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IViewSystem::[[nodiscard]] virtual foundation::Result<ViewId> CreateView(const ViewDesc&desc)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderCommandSink::[[nodiscard]] virtual foundation::Result<void> EndFrame()=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IViewSystem::[[nodiscard]] virtual foundation::Result<void> SetMainView(ViewId view)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IViewSystem::[[nodiscard]] virtual foundation::Result<void> DestroyView(ViewId view)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRendererRuntime::[[nodiscard]] virtual foundation::Result<void> PrepareFrame()=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderCommandSink::[[nodiscard]] virtual foundation::Result<void> AbortFrame()=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> MarkTransformDirty(RenderProxyId id)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> DestroyProxy(RenderProxyId id)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> SetProxyVisibility(RenderProxyId id,RenderProxyVisibility visibility)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRendererRuntime::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderResourceBridge::[[nodiscard]] virtual foundation::Result<void> ReleasePayloads(ResourceId mesh,ResourceId material)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderCommandSink::[[nodiscard]] virtual foundation::Result<void> BeginFrame(const RenderFrameContext&context)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRendererRuntime::[[nodiscard]] virtual foundation::Result<void> RenderFrame()=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<void> MarkMaterialDirty(RenderProxyId id)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderCommandSink::[[nodiscard]] virtual foundation::Result<void> SubmitProxy(const RenderProxySubmission&submission)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderResourceBridge::[[nodiscard]] virtual foundation::Result<void> AcquirePayloads(ResourceId mesh,ResourceId material)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |
| `epidemic::runtime::renderer::IRenderScene::[[nodiscard]] virtual foundation::Result<RenderProxyId> RegisterProxy(const RenderProxyDesc&desc)=0;` | `REVIEWED / real identity, recovery or cleanup boundary` | `TestRendererEnumValidationAndIdExhaustion; TestDeferredDestroyPreservesDeterministicFailedPrefix; TestAbortFailureRetainsRecoveryAndBlocksNewBegin` |

## Lifecycle candidate review

Generated candidate count: 1.

| Candidate | Decision | Evidence |
| --- | --- | --- |
| `epidemic::runtime::renderer::IRendererRuntime::[[nodiscard]] virtual foundation::Result<void> Shutdown()=0;` | `REVIEWED / real terminal lifecycle transition` | `TestShutdownFailureStartsTerminalLifecycleAndIsRetryable; TestShutdownReconcilesOpenFrameBeforeResourceRelease` |

## External-boundary candidate review

Generated candidate count: 1.

| Candidate | Decision | Evidence |
| --- | --- | --- |
| `epidemic::runtime::renderer::IRenderCommandSink::virtual ~IRenderCommandSink()=default;` | `REVIEWED / scanner candidate is destructor; actual command sink boundary reviewed` | `TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestBeginAndEndExceptionFailureMatrix` |

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
| `LIFE-SHUTDOWN` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `LIFE-RETRY-CLEANUP` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `ATOMIC-SINGLE` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `ATOMIC-MULTI` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
| `ATOMIC-EXTERNAL` | `PASS` | Command sink/resource bridge failures retain explicit recovery or cleanup ownership and block unsafe new work. |
| `ATOMIC-RECONCILE` | `PASS` | Reviewed against module implementation, module contract and the mapped Block G regressions. |
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
| `TEST-REGRESSION` | `PASS` | G3-REN-001 remains covered. |

## Defect evidence handoff

| ID | Verified defect / contract | Fix present in this delta or baseline | Regression | CTest target |
| --- | --- | --- | --- | --- |
| `G3-REN-001` | Failed AbortFrame could leave an uncertain backend frame while allowing a new BeginFrame. | Existing `frame_recovery_pending_` reconciliation ownership retained; new Begin is blocked until confirmed Abort. | `renderer_tests.cpp::TestAbortFailureRetainsRecoveryAndBlocksNewBegin; TestShutdownReconcilesOpenFrameBeforeResourceRelease; TestBeginAndEndExceptionFailureMatrix` | `EpidemicRuntimeRendererTests` |

## Qualification recorded by Block G

GCC Debug/Release and Clang Debug/Release pass with warnings-as-errors. Clang ASan + UBSan pass. Official MSVC Debug/Release remains pending in this Linux environment.
