# Goal 3.12 Animation local audit

Status: `LOCAL_READY` candidate after the checks recorded below. This document records the Goal 3.12 local contract only. System-level Resources lease integration and the Animation-to-Renderer reference pose bridge remain owned by Goal 3.17 Support.

## Confirmed fixes

`G3-ANIM-001`: `AnimationRuntime::Tick()` now builds the candidate animator state and pose off-state, stages the immutable pose publication object, calls `IAnimationPoseSink::Publish()` before mutating the live animator, and only after confirmed sink success performs a no-throw move commit. Sink `Result` failure or exception leaves animator snapshot, cached pose, revision and event buffer unchanged. Retry therefore executes the same logical frame once.

`IAnimationPoseSink` explicitly requires failure/exception to mean that the pose was not externally committed. Sinks that cannot provide this property require their own idempotent/deduplicated boundary and are not invented by Animation.

`G3-ANIM-002`: semantic events use fixed bounded storage prepared when the runtime is constructed. Every slot reserves enough name storage for all private semantic event names. Event enqueue after authoritative commit does not grow the container. The existing deterministic overflow policy is preserved: when full, the oldest event is dropped and the newest event is retained. `event_capacity == 0` continues to select the default bound.

`M3C-ANIM-001`: playback scaling больше не использует `long double` как якобы более широкий промежуточный тип. `AdvancePlayback()` теперь раскладывает represented IEEE-754 binary64 rate, умножает integer delta через локальную checked 64x64->128 arithmetic, а дробную часть и carried remainder складывает точно на fixed-point grid с шагом `2^-1074` перед единственным round-to-nearest-even обратно в binary64. `INT64_MAX * 1.0` остаётся допустимым, `nextafter(1.0, 2.0)` reject-ится до mutation, non-zero carried fraction на верхней границе reject-ится, fractional addition не создаёт ложный carry, а сохранённый remainder не получает double-rounding drift. Новый regression `TestPortablePlaybackScalingBoundaries()` закрепляет exact boundary, first-overflow, carried-remainder, post-carry overflow и exact-remainder rounding paths.

`M3C-ANIM-002`: the exact `2^-1074` grid is now authoritative across ticks instead of being rounded back to `double` and reconstructed on the next tick. `AnimatorRecord` keeps the exact private residual alongside the existing mirrored `AnimatorPlayback::fractional_microseconds`. `ScalePlaybackDelta()` consumes and returns the exact grid, while the mirror is derived only for observation/testing. `Play()`, `Stop()` and zero-duration `Crossfade()` clear both forms. The staged `AnimatorRecord` copy keeps the exact residual inside the same candidate-before-publication transaction as local time, revision and pose state. `TestStatefulPlaybackRemainderPartitionAndFailureAtomicity()` reproduces the former three-tick drift, compares partitioned execution with one combined tick, checks evaluator and pose-sink Result/exception rollback, and verifies reset semantics.

## Registry and resource audit

Skeleton and clip IDs/descriptors are validated, duplicates are rejected, clip/skeleton compatibility is checked, registry freeze is idempotent and post-freeze registration is rejected. Resource source failures/exceptions are contained. Resource-backed descriptors are validated before caching or animator publication. ResourceLease ownership is intentionally outside this module and remains a Goal 3.17 Support-adapter responsibility.

## Animator lifecycle audit

Create/destroy handles, stale generation rejection, ID/generation/revision exhaustion, Play/Pause/Stop transitions, invalid clip rejection, crossfade with and without a current clip, loop isolation, finite playback-rate validation, fractional accumulation, LOD transitions and pose state are covered by the local suite. Destroyed handles are rejected by every mutator and pose/snapshot query.

## Evaluation, pose and event audit

Evaluator Result failures and exceptions leave authoritative state untouched. Evaluator output is rejected unless animator handle, owner and skeleton pose size match the requested animator. Pose sink failure/exception is pre-commit. Loop/finished events are queued only after successful pose publication and live commit. `Clear()` changes only the bounded event view. Tick work order remains deterministic by animator ID.

Animation has no local Shutdown contract and none is added for Goal 3.12.

## Qualification

Portable direct qualification of `EpidemicRuntimeAnimationTests` passes with warnings-as-errors in GCC Debug/Release and Clang Debug/Release. Clang AddressSanitizer + UndefinedBehaviorSanitizer + float-cast-overflow also passes. The one-step playback-scaling helper remains covered by the previous independent exact binary-rational oracle. The new cross-tick authoritative remainder was additionally checked through the normal public runtime path across 1,002 deterministic partition cases, including the reproduced `0.1` carry boundary and combined-vs-partitioned execution, with exact rational agreement for local time and the mirrored remainder. Shared generated freeze metadata is intentionally not committed by this parallel module delta; it must be regenerated once after Goal 3 deltas are merged.
