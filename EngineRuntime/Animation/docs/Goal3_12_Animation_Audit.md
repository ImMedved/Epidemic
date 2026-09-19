# Goal 3.12 Animation local audit

Status: `LOCAL_READY` candidate after the checks recorded below. This document records the Goal 3.12 local contract only. System-level Resources lease integration and the Animation-to-Renderer reference pose bridge remain owned by Goal 3.17 Support.

## Confirmed fixes

`G3-ANIM-001`: `AnimationRuntime::Tick()` now builds the candidate animator state and pose off-state, stages the immutable pose publication object, calls `IAnimationPoseSink::Publish()` before mutating the live animator, and only after confirmed sink success performs a no-throw move commit. Sink `Result` failure or exception leaves animator snapshot, cached pose, revision and event buffer unchanged. Retry therefore executes the same logical frame once.

`IAnimationPoseSink` explicitly requires failure/exception to mean that the pose was not externally committed. Sinks that cannot provide this property require their own idempotent/deduplicated boundary and are not invented by Animation.

`G3-ANIM-002`: semantic events use fixed bounded storage prepared when the runtime is constructed. Every slot reserves enough name storage for all private semantic event names. Event enqueue after authoritative commit does not grow the container. The existing deterministic overflow policy is preserved: when full, the oldest event is dropped and the newest event is retained. `event_capacity == 0` continues to select the default bound.

## Registry and resource audit

Skeleton and clip IDs/descriptors are validated, duplicates are rejected, clip/skeleton compatibility is checked, registry freeze is idempotent and post-freeze registration is rejected. Resource source failures/exceptions are contained. Resource-backed descriptors are validated before caching or animator publication. ResourceLease ownership is intentionally outside this module and remains a Goal 3.17 Support-adapter responsibility.

## Animator lifecycle audit

Create/destroy handles, stale generation rejection, ID/generation/revision exhaustion, Play/Pause/Stop transitions, invalid clip rejection, crossfade with and without a current clip, loop isolation, finite playback-rate validation, fractional accumulation, LOD transitions and pose state are covered by the local suite. Destroyed handles are rejected by every mutator and pose/snapshot query.

## Evaluation, pose and event audit

Evaluator Result failures and exceptions leave authoritative state untouched. Evaluator output is rejected unless animator handle, owner and skeleton pose size match the requested animator. Pose sink failure/exception is pre-commit. Loop/finished events are queued only after successful pose publication and live commit. `Clear()` changes only the bounded event view. Tick work order remains deterministic by animator ID.

Animation has no local Shutdown contract and none is added for Goal 3.12.

## Qualification

Portable direct qualification of `EpidemicRuntimeAnimationTests` passes with warnings-as-errors in GCC Debug/Release and Clang Debug/Release. Clang AddressSanitizer + UndefinedBehaviorSanitizer also passes. Shared generated freeze metadata is intentionally not committed by this parallel module delta; it must be regenerated once after Goal 3 deltas are merged.
