# Milestone 4: EngineFramework Local Closure Plan

Дата admission-аудита: 2026-09-22.
Admission code SHA: `bc968d5c7e9e4ed4f221e67bcdde3696e41e0aad` (`dev`, после полного закрытия Goal 3). Архивное имя источника аудита: `Epidemic 22-09-2026-2.zip`.

Этот документ является подробным планом Goal 4 из `work-plan.md`. Он не присваивает `FROZEN`: цель Goal 4 состоит в том, чтобы довести все 52 production-модуля EngineFramework до `LOCAL_READY` по тому же freeze contract из 37 критериев, который уже применён к Base и Runtime.

## 1. Admission baseline, проверенный по текущему дереву

- EngineFramework production modules: **52**.
- Public Framework headers: **59**.
- Public Framework callables: **3054**, classification status уже `REVIEWED`, `UNCLASSIFIED = 0`.
- Mutation/API obligations, которые ещё требуется реально доказать: **3348**.
- Lifecycle candidates: **73**.
- Stale-identity candidates: **770**.
- External-boundary candidates: **17**.
- Все 52 Framework modules сейчас `IN_AUDIT`; у каждого 4 архитектурных критерия уже `PASS`, остальные 33 из 37 `NOT_AUDITED`.
- В `module_dossier_reviews.json` для каждого Framework-модуля reviewed только `Responsibility`: 52 `REVIEWED` поля и 728 `DISCOVERED` поля.
- Canonical Framework contract/test anchors для 3054 callables ещё не сформированы.
- Full Debug/Release manifest содержит 94 tests; Framework имеет отдельные module/integration targets, поэтому Goal 4 не требует изобретать новую test topology.

## 2. Scope Goal 4 и граница с Goals 5-9

Goal 4 закрывает только локальную корректность каждого Framework freeze unit. Для state owner допускаются его собственные snapshot/restore, journal, retry, provider и failure-atomicity контракты. Для IntegrationLayer `LOCAL_READY` доказывается через fakes/ports и локальные durable/checkpoint semantics.

В Goal 4 **не переносятся** следующие системные задачи:

- whole-engine ordered save/load, persistence map и replay determinism: Goal 5;
- sanitizer/lifetime/retention и официальная concurrency qualification: Goal 6;
- реальные multi-owner и cross-layer causal chains: Goal 7;
- broad load, degradation/backpressure и fault/load matrices: Goal 8;
- final whole-engine `FROZEN`: Goal 9.

Это исправляет несколько слишком широких формулировок старого Goal 4. В частности, `SaveGame` сейчас является in-memory orchestration module и не владеет storage/file I/O; такие пункты как «serialization failure», «store failure» и «load read failure» удалены из его локального Goal 4 contract.

## 3. Подтверждённые проблемы admission baseline и конкретный план доработок

Повторный аудит расширил первоначальный список. Сейчас подтверждено **21 defect/blocker IDs**: 2 shared blockers и 19 production/integration defects. В этот список включены только проблемы, механизм которых подтверждается текущим кодом и достижимым состоянием. Scanner candidates и подозрительные конструкции без доказанного нарушения контракта сюда не включены.

Для каждой карточки ниже обязательны отдельный fix, regression и evidence. Если worker обнаружит новый дефект, он добавляет его только после минимального воспроизводимого сценария.

### G4-INFRA-001 · DevelopmentInfrastructure · Shared preflight

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/DevelopmentInfrastructure/Tests/allocation_fault_injection.h:40-50, 592-618`.

Проблема: Framework allocation-failure tests rely on process-global `operator new/new[]/delete` replacement. The helper already contains an MSVC checked-STL exception for a two-pointer allocation, so some allocations are deliberately not injectable and unrelated CRT/STL bookkeeping can be faulted.

Причина: The fault model is attached to the whole process rather than to the mutation/publication boundary under test. MSVC Debug STL/CRT performs its own allocations, so the harness cannot distinguish engine allocation from debug-runtime allocation.

Рекомендуемая доработка: Define one shared narrow fault-seam convention, then migrate each module owned by A01-A20 away from the global allocator. Prefer private implementation seams or dependency-local test callbacks at candidate construction, primary-container publication, index publication and journal/outbox publication. Remove the global override after the last user is migrated.

Особенности окружения: Official qualification is Windows 11 + MSVC `/W4 /WX`. `_ITERATOR_DEBUG_LEVEL` and checked-STL allocations make process-global fault injection especially unreliable in Debug. GCC/Clang portable runs are secondary evidence only.

Риски исправления: A mechanical replacement can reduce coverage by testing only one allocation boundary. Do not add public test hooks, Debug skips, size-based exclusions or catch-all success paths.

Обязательные regressions: For every migrated module, sweep each named fallible boundary and compare the complete pre/post snapshot on failure. Final repository check: no process-global allocation override and no Goal 4 evidence that depends solely on the old helper.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-DOC-001 · Framework docs · Shared preflight

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/README.md:7; missing docs/GameFramework/README.md`.

Проблема: The Framework README points at a non-existent documentation root. Unlike Base and Runtime, Framework currently has no stable 52-module contract/audit documentation tree.

Причина: Framework grew faster than the freeze documentation layer and the old link was never brought into the current `docs/EngineFramework` convention.

Рекомендуемая доработка: Create `docs/EngineFramework/README.md` and stable per-module contract/audit docs or an equivalent generated/module-anchor structure. Each module document must state responsibility, owned state, dependencies, lifecycle, persistence boundary, limits, threading and `LOCAL_READY` status without claiming `FROZEN`.

Особенности окружения: These docs feed human review and canonical anchors. Generated evidence remains single-writer during serial convergence.

Риски исправления: Do not copy stale API descriptions from the old plan. Docs must describe current headers and must not move whole-engine Goal 5-9 responsibilities into Goal 4.

Обязательные regressions: Broken-link check plus canonical dossier/API anchor validation. All 52 module docs must resolve to actual module paths and current public surfaces.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-RB-001 · RuntimeBridge · A03

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/RuntimeBoundary/RuntimeBridge/src/runtime_bridge.cpp:452-459`.

Проблема: `Visible()` subtracts and squares `float` coordinates in `float`. Finite Runtime positions can therefore overflow during subtraction or squaring, producing `inf`, an invalid direction or an invalid ray distance.

Причина: The bridge assumes finite `float` components imply finite `float` distance arithmetic. Runtime World accepts finite transforms without a small-magnitude bound.

Рекомендуемая доработка: Promote components to `double` before subtraction, compute an overflow-safe norm (`std::hypot` or scaled norm), validate representability of the resulting ray distance, then normalize in wide arithmetic before narrowing the unit direction. If the true distance cannot be represented by Runtime `RaycastQuery::max_distance`, return an explicit semantic-range failure instead of constructing a malformed ray.

Особенности окружения: `FLT_MAX` is a valid finite Runtime component. The opposite-sign `±FLT_MAX` case can overflow already at subtraction, not only at squaring.

Риски исправления: Simply casting the already-computed `delta` to `double` is insufficient. Clamping an unrepresentable max distance can create false visibility results.

Обязательные regressions: Finite `FLT_MAX` vs 0, opposite-sign large coordinates, near-zero delta, normal visible/occluded cases, and an unrepresentable true distance. Assert the backend never receives NaN/Inf direction or distance.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-PROC-001 · Processes · A08

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Processes/src/processes.cpp:226-243`.

Проблема: `EvaluateProgress()` computes an integer semantic progress value through `long double`. On MSVC, `long double == double`, so a process one tick before completion can round to 1,000,000 and appear complete.

Причина: The code assumes extended precision when converting very large `int64_t` durations to floating point.

Рекомендуемая доработка: Compute `floor(elapsed * 1'000'000 / total)` with portable overflow-safe integer/rational arithmetic and clamp only after the exact quotient is known.

Особенности окружения: Concrete MSVC boundary: `total=INT64_MAX`, `elapsed=INT64_MAX-1`. Exact result is 999999; binary64 produces 1000000.

Риски исправления: Using `__int128` alone is not portable to MSVC. Reuse or introduce a tested portable mul/div helper without expanding the public API.

Обязательные regressions: One tick before a huge completion boundary, exact half/third fractions, elapsed=0, elapsed=total, paused path, and partitioned time advancement.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-RESPROD-001 · ResourcesProduction · A08

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src/resources_production.cpp:375-470, 610-649, 717-785, 900-970; RestoreSnapshot:1122-1297`.

Проблема: Several create/reserve/transfer APIs advance the real ID generator before revision preflight. A restored `revision=UINT64_MAX` can therefore return `revision_exhausted` after consuming an ID.

Причина: ID generation uses the live generator instead of a staged copy. Restore accepts boundary revision/generator states.

Рекомендуемая доработка: Copy the relevant generator, allocate the candidate ID from the copy, preflight revision/journal/publication capacity, stage containers/indices, then commit generator + revision + state together.

Особенности окружения: The bug is easiest to expose through a crafted valid snapshot because normal execution cannot practically reach `UINT64_MAX`.

Риски исправления: Caller-supplied IDs also call generator-advance helpers. Preserve monotonicity and duplicate semantics while making failures atomic.

Обязательные regressions: For stockpile, node, site, reservation, transaction, capability and plan creation: restore max revision, invoke with auto ID, expect failure and byte/semantic-equivalent snapshot including generator state.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-RESPROD-002 · ResourcesProduction · A08

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src/resources_production.cpp:393-398, 638-648, 761-785, 1314-1323`.

Проблема: Revision/state changes occur before fallible `unordered_map::emplace`, index updates and `changes_.push_back`. Allocation failure can leave an advanced revision, partial primary/index state, or an escaping exception.

Причина: Mutation is committed incrementally instead of staging every fallible publication before the no-fail commit point.

Рекомендуемая доработка: Use candidate primary/index/journal state or guaranteed pre-reserved capacity. Stage all allocations first, then commit by no-throw swap/move/update. Add private fault seams at each publication phase.

Особенности окружения: MSVC Debug allocation behavior is exactly why the old global allocator harness is not acceptable evidence.

Риски исправления: Blindly copying every large map for every mutation may create unacceptable complexity. Prefer narrow candidate objects, `reserve`, node handles or local staged deltas where no-throw commit can be proven.

Обязательные regressions: Fault injection at primary insert, reserved-index insert, transaction publication and journal append. On failure verify revision, generator, primary maps, derived indices and journal are unchanged.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-SOC-001 · Society · A11

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Society/include/Epidemic/GameFramework/Society/society.h:423-428; src/society.cpp:328-355, 376-423, 466-525; RestoreSnapshot:818-1022`.

Проблема: Several Society mutators ignore the `bool` result of `Bump()`. After restoring `revision=UINT64_MAX`, they still modify memberships, relationships or reputation while the global revision does not advance.

Причина: Some newer paths check `Bump()`, older paths retained fire-and-forget calls.

Рекомендуемая доработка: Centralize revision preflight and require it before every revision-bearing mutation. Mutations must fail before changing authoritative/derived state when the revision is exhausted.

Особенности окружения: Restore accepts max revision, so the boundary is reachable in tests and persisted state even if normal runtime would never perform 2^64 mutations.

Риски исправления: No-op/idempotent calls should remain successful without consuming revision if that is the current contract. Do not force bumps for true no-ops.

Обязательные regressions: Max-revision restore regressions for SetMembershipRole, RemoveMembership, existing/new SetRelationship, ApplySocialChange and SetReputation. Verify full snapshot equality on failure.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-SOC-002 · Society · A11

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Society/src/society.cpp:283-307, 397-423, 1035-1044`.

Проблема: Membership/relationship ID generators and revision can advance before fallible map/index/journal publication. `Record()` performs a potentially allocating `deque::push_back` after state changes.

Причина: The service commits generator, primary state, derived indices and journal in multiple fallible steps.

Рекомендуемая доработка: Stage generator copies, primary record, all derived index inserts and journal append before committing. Define a single no-fail commit point.

Особенности окружения: Use narrow module-local fault seams after G4-INFRA-001. The existing global allocator helper is not sufficient.

Риски исправления: Society has bidirectional/semantic indices. A fix that protects only the primary map can still leave index corruption.

Обязательные regressions: Allocation/revision failures for AddMembership, RemoveMembership, new/existing relationship and reputation. Validate both query directions, generators, revision and journal.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-PER-001 · Perception · A12

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:300-307, 339-360`.

Проблема: Spatial deltas are formed after converting each `int64_t` millimetre coordinate to `long double`. On MSVC adjacent coordinates above 2^53 can become identical, corrupting distance and FOV decisions.

Причина: The implementation assumes `long double` has more integer precision than binary64.

Рекомендуемая доработка: Compute signed coordinate differences exactly before floating conversion, with an overflow-safe signed-difference representation, then use wide/scaled norm and dot-product logic. Do not subtract potentially opposite-sign int64 values directly in int64.

Особенности окружения: MSVC x64 defines `long double` with the same precision as `double`.

Риски исправления: A naive `a-b` in `int64_t` introduces signed overflow at opposite extremes. The fix must handle `[INT64_MIN, INT64_MAX]` coordinates.

Обязательные regressions: `2^53` vs `2^53+1`, opposite-sign extremes, axis-aligned and diagonal FOV thresholds, zero-length target vector and deterministic threshold equality.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-PER-002 · Perception · A12

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:930-970`.

Проблема: Awareness decay advances `last_decay_at` through `long double(steps) * long double(interval)`. On MSVC a mathematically valid value below `INT64_MAX` can round up to the boundary and advance time too far.

Причина: An exactly bounded integer product is unnecessarily converted through binary64.

Рекомендуемая доработка: After `steps = elapsed / interval`, compute the advance using checked integer multiplication. The product is mathematically <= elapsed, so this can be implemented without floating point and without overflow.

Особенности окружения: Concrete case: `last=0`, `now=INT64_MAX-1`, `interval=1`. Exact advance is `INT64_MAX-1`; binary64 rounds it to 2^63 and the current clamp chooses `INT64_MAX`.

Риски исправления: Preserve the current number-of-whole-intervals semantics. Do not change decay cadence or suspicion thresholds while fixing timestamp arithmetic.

Обязательные regressions: The concrete max-boundary case, exact-multiple/non-multiple intervals, repeated partitioned calls vs one combined call, and no overshoot of `now`.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-PER-003 · Perception · A12

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:300-336; RegisterSense:68-80`.

Проблема: `DistanceSquared()` saturates the squared distance into signed 64-bit `Fixed` before `DistanceMm()` takes the square root. Every real distance above `sqrt(INT64_MAX) ≈ 3,037,000,499 mm` collapses to roughly that same distance. Since `base_range_mm` has no corresponding upper bound, far targets can be falsely treated as in range.

Причина: Squared-distance storage uses the same 64-bit semantic type as linear distance.

Рекомендуемая доработка: Do not represent the intermediate square in `Fixed`. Compute linear distance directly with overflow-safe wide/scaled arithmetic, or compare exact/wide squared distance to squared range without narrowing. Only the final linear result should saturate to `Fixed` if required by contract.

Особенности окружения: This is toolchain-independent. Example: true distance 4,000,000,000 mm with range 3,500,000,000 mm is out of range, but the current saturated square yields approximately 3,037,000,499 mm.

Риски исправления: Changing distance math affects attenuation and identification thresholds. Preserve truncation/rounding semantics explicitly and update only tests that encoded the incorrect saturation.

Обязательные regressions: Distances below/at/above `sqrt(INT64_MAX)`, 4e9 vs 3.5e9 range, very large diagonal coordinates, attenuation monotonicity and FOV consistency.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-COMBAT-001 · Combat · A14

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Combat/src/combat.cpp:58-70, 286-307`.

Проблема: `ScaleRatioSat()` uses `long double` for exact-looking int64 ratio scaling. On MSVC this is binary64 and can produce the wrong integer near the int64 boundary.

Причина: The helper relies on floating-point precision to decide an integer quotient and saturation boundary.

Рекомендуемая доработка: Replace it with portable exact/saturating signed mul-div arithmetic. Define truncation toward zero explicitly.

Особенности окружения: Concrete MSVC case: `M=INT64_MAX`, `value=M-1`, `old_max=M`, `new_max=M-1`. Exact truncation is `M-2`; the current binary64 path rounds all three inputs to 2^63 and returns/clamps to `M`.

Риски исправления: MSVC has no standard `__int128`. Do not introduce a compiler-only implementation without a portable fallback and cross-toolchain tests.

Обязательные regressions: The concrete boundary, min/max signed combinations allowed by the API, zero/negative policy cases, preserve-ratio monotonicity and normal small values.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-PROG-001 · Progression · A15

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:305-319`.

Проблема: `RemoveProfile()` erases the profile before checking global revision exhaustion. It can return failure after authoritative state was removed.

Причина: Revision preflight is ordered after destructive mutation.

Рекомендуемая доработка: Check revision/journal publication capacity first, stage the journal record, then erase and publish in the no-fail commit phase.

Особенности окружения: Boundary is reachable by RestoreSnapshot with max revision.

Риски исправления: Profile removal also interacts with pending reservations. Preserve the existing reservation rejection semantics.

Обязательные regressions: Max revision and journal-publication failure preserve profile, tracks, modifiers, unlocks, indices/generators and journal.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-PROG-002 · Progression · A15

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:372-384, 747-799, 803-905, 1070-1097`.

Проблема: Several mutators alter profile state and then call `Bump()` while ignoring failure. `CommitProgressGrant()` can erase the reservation before proving revision capacity.

Причина: Revision and reservation lifecycle are not treated as part of the transaction.

Рекомендуемая доработка: Precompute all required revision increments and journal records, verify capacity, stage profile/reservation changes, then commit atomically. `CommitProgressGrant()` should consume its reservation only when the terminal outcome is durably determined.

Особенности окружения: Restore can inject max global/profile revisions and exhausted journal sequence.

Риски исправления: Milestone evaluation can emit multiple changes. Preflight the full count, not only one revision, and avoid double-granting unlocks on retry.

Обязательные regressions: Max global/profile revision for SetBaseAttribute, grant/revoke perk/unlock, progress commit and milestone event fan-out. Retry must not duplicate rewards.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-PROG-003 · Progression · A15

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:462-492`.

Проблема: `AddModifier()` calls the live `modifier_ids_.Next()` and advances the generator before `CanBump()` and before `profile->modifiers.push_back`. A failed revision check or allocation can consume an ID.

Причина: ID generation is not staged with the rest of the mutation.

Рекомендуемая доработка: Use a staged generator copy and staged modifier publication; commit generator only after revision/journal and container publication are guaranteed.

Особенности окружения: This is visible in snapshots because modifier generator state is persisted.

Риски исправления: Caller-supplied modifier IDs must still advance the generator past accepted IDs without allowing duplicate reuse.

Обязательные regressions: Revision exhaustion and allocation failure leave modifier list and generator snapshot unchanged; caller-supplied high ID still advances the generator on success.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-TRAV-001 · Traversal · A16

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Traversal/include/Epidemic/GameFramework/Traversal/traversal.h:470-475; src/traversal.cpp:215-260, 405-482, 540-678; RestoreSnapshot:829-943`.

Проблема: Most Traversal mutators ignore the `bool` result of `Bump()`. Restoring `revision=UINT64_MAX` allows state/session/route/carrier mutation with a stale revision.

Причина: Only a few older/newer paths use checked revision handling; the service lacks one enforced mutation preflight.

Рекомендуемая доработка: Require a checked next revision before every state-changing operation. Stage state/session/route/binding changes and publish only after preflight.

Особенности окружения: Restore accepts max revision. Journal exhaustion (`next_change_sequence=0`) is already intentionally tested as snapshot-required mode and is not automatically the same defect.

Риски исправления: Do not conflate revision exhaustion with the explicitly supported terminal journal behavior. Preserve documented idempotent/no-op outcomes.

Обязательные regressions: Max revision for capability grant/revoke, mode change, route add/remove, start/suspend/resume/finalize session, board and disembark. Full snapshot unchanged on failure.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-TRAV-002 · Traversal · A16

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Traversal/src/traversal.cpp:552-564, 648-653, 965-974 and analogous route/grant paths`.

Проблема: After revision advancement, Traversal performs fallible map/deque publication (`emplace`, `Record::push_back`) and multi-container updates. Allocation failure can leave partial state or an advanced revision.

Причина: No common staged publication transaction exists for session/route/grant/binding mutations.

Рекомендуемая доработка: Stage ID generator, primary record, derived references and journal entry before the commit point. Use private fault seams at each publication phase.

Особенности окружения: Use the new Goal 4 narrow fault-injection convention, not global `operator new`.

Риски исправления: Session state is cross-linked from `TraversalState::active_session`; both sides must commit or roll back together.

Обязательные regressions: Fault each session/route/grant/carrier publication boundary and assert state/session cross-links, generators, revision and journal remain coherent.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-NARRINT-001 · NarrativeIntegration · A18

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/IntegrationLayer/NarrativeIntegration/include/Epidemic/GameFramework/NarrativeIntegration/narrative_adapters.h:164-183; src/narrative_adapters.cpp:709-749`.

Проблема: `NarrativeExternalConsequenceOutbox::Bump()` unconditionally increments revision. Restore accepts a snapshot revision, including the maximum value, so the next mutation can wrap to zero.

Причина: The outbox has persisted revision state but no checked-next semantics.

Рекомендуемая доработка: Introduce private checked next-revision/preflight and reject mutation before state change on exhaustion. Validate all outbox state transitions, not only Execute.

Особенности окружения: Boundary comes from persisted/restore state and is therefore valid Goal 4 local evidence.

Риски исправления: Do not change stable external operation IDs or retry/idempotence semantics while adding revision checks.

Обязательные regressions: Restore max revision and exercise Execute, retry/acknowledge/fail/prune transitions. No revision wrap and no state change on exhaustion.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-NARRINT-002 · NarrativeIntegration · A18

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/IntegrationLayer/NarrativeIntegration/src/narrative_adapters.cpp:551-595`.

Проблема: `Execute()` increments outbox revision before `deliveries_.push_back`, which can allocate. Allocation failure can advance revision without creating the durable delivery record.

Причина: Metadata is committed before the fallible outbox publication.

Рекомендуемая доработка: Build/stage the delivery and ensure vector capacity/publication first, then commit revision and record together. A private fail point should sit immediately before publication.

Особенности окружения: The outbox is the durability boundary for external consequences, so false publication is especially dangerous for retries.

Риски исправления: Do not execute any external consequence in this path while fixing local durability. Preserve duplicate execution matching semantics.

Обязательные regressions: Allocation failure before publication, duplicate Execute after failure, capacity boundary, restore/retry and terminal acknowledgement.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-EXTINT-001 · ExtendedGameplayIntegration · A20

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/src/extended_gameplay_adapters.cpp:410-456`.

Проблема: `TradeCoordinator::Prepare()` allocates an execution ID before validating every goods line. Ordinary invalid input can therefore return failure after changing the persisted execution ID generator.

Причина: Generator mutation is performed before the full validation/staging phase.

Рекомендуемая доработка: Validate the complete plan first or allocate from a staged generator copy. Commit the generator only together with the durable local execution record.

Особенности окружения: `CaptureSnapshot()` includes coordinator generator/execution state, so this is directly observable failure atomicity.

Риски исправления: Preserve deterministic correlation between trade execution ID and the economy transaction ID (`plan.money.id = id.value`).

Обязательные regressions: Party-valid but malformed goods, duplicate item, unavailable item and ownership mismatch must leave coordinator snapshot/generator unchanged.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

### G4-EXTINT-002 · ExtendedGameplayIntegration · A20

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/src/extended_gameplay_adapters.cpp:458-536`.

Проблема: Goods and money reservations are accepted by external owners before the local `executions_.emplace`. If local publication allocates/fails, external work can exist without a durable local execution/reconciliation record. Rollback-failure branches also rely on a late `emplace`.

Причина: The coordinator creates its durable ownership/reconciliation record after accepted external side effects.

Рекомендуемая доработка: Publish a durable local Prepared/Staging execution record, or otherwise guarantee non-throwing local capacity, before the first external reservation. Update the record after each accepted leg. If rollback fails, reconciliation state must already exist.

Особенности окружения: This is a local IntegrationLayer contract and belongs to Goal 4 even though real multi-owner causal-chain stress remains Goal 7.

Риски исправления: A premature record must not be mistaken for a fully prepared transaction after restore. Define explicit staging/reconciliation states and retry rules.

Обязательные regressions: Local publication failure before first external call means zero external calls. Failure after one accepted goods leg leaves a durable reconciliation record. Retry is idempotent and does not double-reserve.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

## 4. Окружение, общие риски и обязательный preflight

### 4.1. Официальное окружение Goal 4

- Основной qualification target: Windows 11, MSVC, C++23, `/W4 /WX`.
- На MSVC x64 `long double` имеет ту же точность, что `double`. Нельзя использовать `long double` как «более широкий» integer-boundary тип.
- ClangCL public-header/public-surface остаётся обязательным remote CI gate на финальном SHA.
- Корневой CMake содержит Windows platform gate. GCC/Clang portable builds полезны как дополнительная проверка, но не заменяют официальный MSVC admission.
- `std::vector`, `std::deque`, `std::unordered_map` и их `push_back/emplace/reserve` считаются fallible allocation boundaries. `Result` API не должен уже изменить authoritative state до такого boundary, если контракт не описывает durable reconciliation.
- Snapshot/restore является допустимым способом создать граничные состояния `UINT64_MAX`, исчерпанный generator/cursor/sequence и stale identities. Нельзя отбрасывать такие regressions как «нереалистичные».
- Test-only hooks не выводятся в public headers. Допустимы private implementation seams, internal friend/test adapters или dependency-local fake fail points.

### 4.2. Что сделать до запуска A01-A20

1. Зафиксировать текущий admission SHA и baseline тестов.
2. Определить единый безопасный fault-seam API/паттерн для Framework tests. Не требуется последовательно переписать все 31 test translation units до старта workers. Каждый Axx worker мигрирует fault injection только для своих модулей, но никакой новый Goal 4 evidence не принимается, если он зависит только от process-global allocator.
3. Создать `docs/EngineFramework/README.md`, шаблон module contract doc и исправить broken link из `EngineFramework/README.md`.
4. Создать `_goal4_handoff/A01..A20` и зафиксировать single-writer rule: workers не редактируют canonical `docs/freeze/*.json`, общий `work-plan.md`, top-level/shared CMake и чужие production directories.
5. Сгенерировать для каждого worker exact список его callable signature IDs, obligations, lifecycle/stale/external candidates и dossier fields.
6. После preflight shared helper changes повторно прогнать Framework/Full Debug хотя бы один раз, чтобы общий harness не стал источником массового ложного регресса.

### 4.3. Карта миграции старого allocator harness

`allocation_fault_injection.h` сейчас включён как минимум в 31 Framework test translation unit: Interaction, Loot, Time, Processes, Narrative, Facts, RolesJobs, Simulation, Knowledge, Environment, NeedsLife, Materials, Traversal, Equipment, Effects, Perception, Society, Entities, AI, ItemsInventory, Foundation, World, Encounters, Ownership, Progression, NavigationSemantics, Population, SaveGame, Abilities, Queries и ResourcesProduction.

Каждый соответствующий Axx block обязан либо полностью удалить зависимость своего Goal 4 evidence от этого helper, либо явно показать, что test не использует global allocation replacement. После serial convergence repository-wide поиск process-global allocator override должен дать 0.

## 5. Единый инженерный contract для каждого из 20 потоков

Worker не получает задачу «посмотреть модуль». Он обязан пройти следующий порядок и закончить модуль до состояния, которое можно механически принять в canonical evidence.

### 5.1. Сначала доказать контракт

1. Ответственность и граница ownership.
2. Public API и семантика success/no-op/invalid input/failure.
3. Authoritative и derived state, все вторичные индексы и cross-references.
4. ID/generator/revision/epoch/cursor/sequence exhaustion.
5. Lifecycle/state machine и terminal states.
6. External providers/callbacks/backends, accepted-work и retry/reconciliation.
7. Snapshot/restore, persistent/transient state, generator continuity, invalid snapshot rejection.
8. Deterministic ordering всех API, где порядок является наблюдаемым.
9. Threading contract, даже если он `single-thread only`.
10. Capacity/retention/budget/complexity limits.

### 5.2. Затем проверить mutation transaction

Для каждой mutation API worker обязан нарисовать локальную последовательность:

`validate -> compute/stage -> reserve fallible capacity -> external accepted-work boundary -> no-fail commit -> publish journal/outbox -> return`.

Если текущий код имеет fallible operation после изменения authoritative state, worker либо доказывает документированный reconciliation contract, либо исправляет порядок.

Отдельно проверяются generator, revision и journal. Они являются частью observable state и не могут «слегка измениться» на failed Result.

### 5.3. Snapshot/restore minimum

Для каждого snapshot owner обязательны:

- non-empty roundtrip;
- deterministic canonical capture;
- corrupt/duplicate/cross-reference rejection;
- generator and cursor continuity;
- max revision/sequence/generator boundary where representable;
- restore failure leaves previous live state unchanged;
- successful restore rebuilds/validates every derived index;
- transient/provider pointers are not persisted unless contract explicitly says otherwise.

### 5.4. External boundary minimum

Для каждого callback/provider/backend:

- failure before accepted work;
- accepted work followed by local failure;
- duplicate call;
- retry after partial progress;
- stale generation/revision/cursor;
- terminal failure;
- restore/restart with pending work;
- local durable record exists before non-reversible external side effect whenever the module owns retry.

### 5.5. Что worker не должен делать

- не расширять public API ради теста без отдельного API review;
- не переводить scanner candidate в defect без reproduction;
- не «исправлять» journal terminal mode, если текущий контракт явно разрешает snapshot-required continuation;
- не присваивать `FROZEN`;
- не закрывать Goal 5-9 системные требования внутри Goal 4;
- не менять production code только потому, что dossier/ledger пока `IN_AUDIT`.

### 5.6. Обязательный output каждого Axx

`_goal4_handoff/<Axx>/`:

- `dossier_reviews.json`;
- `coverage_reviews.json`;
- `public_api_anchors.json`;
- `defects.json`;
- `notes.md`;
- список changed files;
- точные test targets/commands;
- статус каждого owned module: `LOCAL_READY_CANDIDATE` либо конкретный blocker.

В `defects.json` для каждого defect обязательны: ID, module, root cause, touched files, regression target, regression symbol, failure mode, proof before/after.

## 6. Разбиение на 20 параллельных потоков

A01-A20 — это прежде всего 20 неизменных single-writer ownership blocks, а не требование одновременно держать 20 workers. При меньшей доступной capacity блоки выполняются волнами; один worker может последовательно закрыть несколько блоков, но production directory нельзя передавать другому worker без явного handoff. Специальная схема для 16 workers приведена в разделе 7.

Admission workload намеренно неравномерен из-за ownership boundaries: A20 содержит 312 obligations, A19 — 240, A16 — 220, тогда как A03 — 88. Чтобы эти блоки не стали скрытым critical path, при волновом запуске A20/A19/A16 следует стартовать первыми; готовность всё равно принимается по полному block handoff, а не по числу одновременно занятых workers.

Числа в таблицах являются admission workload, а не оценкой дефектности: API = public callables, obligations = ещё не закрытые mutation obligations, life/stale/ext = candidate records для ручного review.

| Block | Modules | API | Obligations | Life | Stale | External |
|---|---|---:|---:|---:|---:|---:|
| A01 | Foundation, SupportRandom | 132 | 96 | 2 | 9 | 0 |
| A02 | Queries, Facts, Time | 123 | 160 | 3 | 36 | 1 |
| A03 | RuntimeBridge | 71 | 88 | 0 | 21 | 3 |
| A04 | Entities, Materials, Environment | 186 | 152 | 5 | 35 | 0 |
| A05 | Conditions, Effects, Interaction | 154 | 168 | 3 | 38 | 0 |
| A06 | ItemsInventory, Equipment | 154 | 164 | 2 | 41 | 0 |
| A07 | Ownership, Economy | 168 | 160 | 1 | 39 | 0 |
| A08 | Processes, ResourcesProduction | 181 | 172 | 5 | 41 | 7 |
| A09 | RolesJobs, NeedsLife | 138 | 152 | 6 | 37 | 0 |
| A10 | Population, Encounters | 145 | 148 | 4 | 33 | 0 |
| A11 | Society, Crime | 162 | 136 | 2 | 33 | 0 |
| A12 | Perception, Knowledge, NavigationSemantics | 181 | 164 | 3 | 39 | 1 |
| A13 | AI, Simulation | 135 | 144 | 6 | 35 | 0 |
| A14 | Combat, Loot | 104 | 144 | 2 | 32 | 0 |
| A15 | Abilities, Progression | 120 | 192 | 2 | 48 | 0 |
| A16 | Construction, Traversal | 190 | 220 | 8 | 50 | 0 |
| A17 | World, SaveGame, WorldIntegration | 128 | 152 | 2 | 32 | 1 |
| A18 | Narrative, NarrativeIntegration | 212 | 184 | 5 | 45 | 0 |
| A19 | Dialogue, Integration, StateIntegration, InteractionTimeIntegration, InteractionEffectsIntegration | 170 | 240 | 6 | 51 | 2 |
| A20 | GameplayIntegration, ExtendedGameplayIntegration, PerceptionKnowledgeAIIntegration, PopulationSimulationIntegration, ProcessResourceSimulationIntegration, SocialLegalIntegration, TraversalNavigationConstructionIntegration | 200 | 312 | 6 | 75 | 2 |

### A01. Foundation, SupportRandom

Admission workload: 132 API, 96 mutation obligations, 2 lifecycle, 9 stale-identity, 0 external-boundary candidates, 9 public headers.

#### Foundation

Freeze unit: `EngineFramework/BaseInfrastructure/Foundation`. Admission API: 104, obligations: 32, lifecycle: 2, stale: 7, external: 0.

- [ ] Все gameplay IDs, refs, tags и handles имеют однозначную invalid/stale semantics.
- [ ] IdGenerator snapshot/restore и exhaustion.
- [ ] ChangeCursor ordering и stale cursor behavior.
- [ ] TypeRegistry duplicate registration.
- [ ] TypeRegistry freeze.
- [ ] GameplayContext не создаёт hidden ownership.
- [ ] Gameplay time/value types boundary arithmetic.

#### SupportRandom

Freeze unit: `EngineFramework/BaseInfrastructure/SupportRandom`. Admission API: 28, obligations: 64, lifecycle: 0, stale: 2, external: 0.

- [ ] Один seed и одна sequence дают одинаковые значения.
- [ ] Snapshot/restore random sequence продолжает тот же stream.
- [ ] Разные streams не делят hidden mutable state.
- [ ] Boundary ranges не имеют modulo bias, если contract обещает uniform result.
- [ ] Invalid range отклоняется.
- [ ] Randomness не зависит от wall clock после создания stream.

Block exit:

- [ ] Все модули A01 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A02. Queries, Facts, Time

Admission workload: 123 API, 160 mutation obligations, 3 lifecycle, 36 stale-identity, 1 external-boundary candidates, 3 public headers.

#### Queries

Freeze unit: `EngineFramework/BaseInfrastructure/Queries`. Admission API: 43, obligations: 24, lifecycle: 1, stale: 4, external: 0.

- [ ] Provider registration, duplicate и freeze.
- [ ] Query type mismatch.
- [ ] Consistency/coverage/accuracy requirements.
- [ ] Budget exhaustion.
- [ ] Snapshot coordinator epoch.
- [ ] Provider failure не оставляет partial response state.
- [ ] Deterministic provider order.
- [ ] Detached query response lifetime.
- [ ] Diagnostics не является authoritative state.

#### Facts

Freeze unit: `EngineFramework/BaseInfrastructure/Facts`. Admission API: 49, obligations: 76, lifecycle: 1, stale: 17, external: 1.

- [ ] Fact assert/update/remove lifecycle.
- [ ] Event publication.
- [ ] History policy.
- [ ] Transaction commit/rollback.
- [ ] Duplicate fact/event identities.
- [ ] Type registration и freeze.
- [ ] Subscriber reentrancy.
- [ ] Subscriber failure policy.
- [ ] Journal/history cursor.
- [ ] Compaction не удаляет необходимый retained history.
- [ ] Snapshot/restore persistent facts, events, generator and cursor.
- [ ] Failed transaction или allocation не публикует half-state.

#### Time

Freeze unit: `EngineFramework/BaseInfrastructure/Time`. Admission API: 31, obligations: 60, lifecycle: 1, stale: 15, external: 0.

- [ ] Clock registration/state.
- [ ] Schedule create/update/remove.
- [ ] Recurrence daily/calendar/custom boundaries.
- [ ] Catch-up policies.
- [ ] Large time jump.
- [ ] Duplicate schedule identity.
- [ ] Cancel terminal semantics.
- [ ] Trigger order deterministic при одинаковом timestamp.
- [ ] Scheduler budget exhaustion deferred, без потери triggers.
- [ ] Journal/cursor consistency.
- [ ] Snapshot/restore clocks, schedules, generators, cursor.

Block exit:

- [ ] Все модули A02 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A03. RuntimeBridge

Admission workload: 71 API, 88 mutation obligations, 0 lifecycle, 21 stale-identity, 3 external-boundary candidates, 1 public headers.

Подтверждённые defects этого потока:

- `G4-RB-001`: RuntimeBridge visibility arithmetic can overflow even for finite Runtime positions; promote before subtraction, use an overflow-safe norm, reject unrepresentable ray distance. Место: `EngineFramework/RuntimeBoundary/RuntimeBridge/src/runtime_bridge.cpp:452-459`.

#### RuntimeBridge

Freeze unit: `EngineFramework/RuntimeBoundary/RuntimeBridge`. Admission API: 71, obligations: 88, lifecycle: 0, stale: 21, external: 3.

- [ ] Framework semantic object creates exactly one runtime materialization per binding generation.
- [ ] Runtime handle/generation forward and reverse maps remain mutually consistent.
- [ ] Create/update/remove projection commands and dematerialization cleanup are retry-safe.
- [ ] Runtime backend failure must not falsely publish semantic success.
- [ ] Stale runtime observation cannot apply to a newer Framework generation.
- [ ] Duplicate command/event delivery is idempotent or rejected by the documented contract.
- [ ] Queue ordering, coalescing, backpressure and processing budgets preserve accepted work.
- [ ] World/Physics/Environment/Navigation observations validate finite numeric data and semantic ranges.
- [ ] Visibility distance/direction math remains valid for every finite runtime position representable by the Runtime contract.
- [ ] Checkpoint/reconciliation state, if present, restores mappings without orphaned Runtime representations.

Block exit:

- [ ] Все модули A03 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A04. Entities, Materials, Environment

Admission workload: 186 API, 152 mutation obligations, 5 lifecycle, 35 stale-identity, 0 external-boundary candidates, 3 public headers.

#### Entities

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Entities`. Admission API: 75, obligations: 52, lifecycle: 3, stale: 11, external: 0.

- [ ] Entity create/remove/deactivate/convert lifecycle.
- [ ] Archetype and part references.
- [ ] Tags and parts indexes.
- [ ] Stale entity/part references.
- [ ] Convert сохраняет только разрешённые fields.
- [ ] Pruning history не повреждает latest cursor.
- [ ] Snapshot/restore records, indexes, generators, revisions, journal.

#### Materials

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Materials`. Admission API: 67, obligations: 60, lifecycle: 1, stale: 14, external: 0.

- [ ] Material/substance/type registration.
- [ ] Composition validation и normalization.
- [ ] Slot assignment.
- [ ] Stimulus/exposure mutation.
- [ ] Cross-reference validity.
- [ ] Derived material response соответствует primary composition.
- [ ] Journal prune boundary.
- [ ] Snapshot/restore.

#### Environment

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Environment`. Admission API: 44, obligations: 40, lifecycle: 1, stale: 10, external: 0.

- [ ] Gameplay environment definitions and state.
- [ ] Region/area references.
- [ ] Weather/environment semantic mutation.
- [ ] Expiration/sweep.
- [ ] Derived queries match authoritative state.
- [ ] No duplicate semantic state against Runtime Environment.
- [ ] Snapshot/restore.

Block exit:

- [ ] Все модули A04 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A05. Conditions, Effects, Interaction

Admission workload: 154 API, 168 mutation obligations, 3 lifecycle, 38 stale-identity, 0 external-boundary candidates, 3 public headers.

#### Conditions

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Conditions`. Admission API: 50, obligations: 60, lifecycle: 1, stale: 14, external: 0.

- [ ] Definition registration и freeze.
- [ ] Apply/remove.
- [ ] Stacking policies.
- [ ] Persistence/materialization policies.
- [ ] Periodic catch-up.
- [ ] Expiration.
- [ ] Subject provider failure.
- [ ] Duplicate condition handling.
- [ ] Snapshot/restore instances, generators, revisions, journal.

#### Effects

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Effects`. Admission API: 57, obligations: 60, lifecycle: 1, stale: 13, external: 0.

- [ ] Definition/handler registration и freeze.
- [ ] Immediate execution.
- [ ] Deferred execution.
- [ ] Cancel deferred.
- [ ] Cancel deferred targeting.
- [ ] Take deferred by schedule.
- [ ] Multi-operation batch prepare/commit.
- [ ] Handler prepare failure.
- [ ] Handler commit failure.
- [ ] Target state provider failure.
- [ ] Execution budget.
- [ ] Journal pruning.
- [ ] Snapshot/restore deferred queue, IDs, revisions and journal.

#### Interaction

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Interaction`. Admission API: 47, obligations: 48, lifecycle: 1, stale: 11, external: 0.

- [ ] Interaction definition/session/request lifecycle.
- [ ] Start/advance/complete/cancel.
- [ ] Preconditions and state provider.
- [ ] Executor success/failure.
- [ ] Timeout/sweep.
- [ ] Duplicate execution prevention.
- [ ] Reservation/pending state rollback.
- [ ] Snapshot/restore active interactions and pending execution state.

Block exit:

- [ ] Все модули A05 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A06. ItemsInventory, Equipment

Admission workload: 154 API, 164 mutation obligations, 2 lifecycle, 41 stale-identity, 0 external-boundary candidates, 2 public headers.

#### ItemsInventory

Freeze unit: `EngineFramework/GameplayWorldStateOwners/ItemsInventory`. Admission API: 82, obligations: 92, lifecycle: 1, stale: 23, external: 0.

- [ ] Item definition and container registration.
- [ ] Item create/remove.
- [ ] Stack split/merge rules.
- [ ] Capacity and slot limits.
- [ ] Transfer within/between containers.
- [ ] Reservation create/consume/release.
- [ ] Prepare/commit/cancel transfer.
- [ ] Exchange reservations.
- [ ] Durability and charges boundaries.
- [ ] World/container bindings.
- [ ] Secondary container/item indexes.
- [ ] Failed transfer leaves source and target unchanged.
- [ ] Snapshot/restore items, containers, reservations, bindings, generators, journal.

#### Equipment

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Equipment`. Admission API: 72, obligations: 72, lifecycle: 1, stale: 18, external: 0.

- [ ] Equipment slot definition.
- [ ] Equip/unequip.
- [ ] Prepare equip.
- [ ] Item provider reservation.
- [ ] Conflicting slots.
- [ ] Requirements and compatibility.
- [ ] Reconcile restored bindings.
- [ ] Exchange/reconcile reservations.
- [ ] Provider failure before and after prepare.
- [ ] Snapshot/restore equipment and external binding metadata.

Block exit:

- [ ] Все модули A06 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A07. Ownership, Economy

Admission workload: 168 API, 160 mutation obligations, 1 lifecycle, 39 stale-identity, 0 external-boundary candidates, 2 public headers.

#### Ownership

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Ownership`. Admission API: 63, obligations: 52, lifecycle: 0, stale: 12, external: 0.

- [ ] Owner/property identity.
- [ ] Assign/transfer/remove ownership.
- [ ] Duplicate ownership prevention.
- [ ] Shared/fractional semantics, если предусмотрены.
- [ ] Stale owner/property references.
- [ ] Transfer atomicity.
- [ ] Journal/revision.
- [ ] Snapshot/restore.

#### Economy

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Economy`. Admission API: 105, obligations: 108, lifecycle: 1, stale: 27, external: 0.

- [ ] Currency/account registration.
- [ ] Funds reservation lifecycle.
- [ ] Monetary transfer.
- [ ] Offer lifecycle.
- [ ] Trade transaction lifecycle.
- [ ] Debt and contract lifecycle.
- [ ] Price provider failure.
- [ ] No negative/overflow balance unless explicitly allowed.
- [ ] Reservation prevents double spending.
- [ ] Transfer failure leaves both accounts unchanged.
- [ ] Snapshot/restore accounts, reservations, offers, trades, debts, contracts, generators, journal.

Block exit:

- [ ] Все модули A07 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A08. Processes, ResourcesProduction

Admission workload: 181 API, 172 mutation obligations, 5 lifecycle, 41 stale-identity, 7 external-boundary candidates, 2 public headers.

Подтверждённые defects этого потока:

- `G4-PROC-001`: Processes progress ratio uses `long double`; on MSVC a process one tick before `INT64_MAX` duration can report 100%. Replace with exact portable integer mul/div. Место: `EngineFramework/GameplayWorldStateOwners/Processes/src/processes.cpp:226-243`.
- `G4-RESPROD-001`: ResourcesProduction consumes live ID generators before revision preflight, so failed operations can change persisted generator state. Место: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src/resources_production.cpp:375-470,610-649,717-785,900-970`.
- `G4-RESPROD-002`: ResourcesProduction advances revision/state before fallible primary/index/journal publication. Stage fallible work before the no-fail commit point. Место: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src/resources_production.cpp:393-398,638-648,761-785,1314-1323`.

#### Processes

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Processes`. Admission API: 93, obligations: 88, lifecycle: 4, stale: 20, external: 4.

- [ ] Process definition/execution lifecycle.
- [ ] Input reservations.
- [ ] Output prepare/commit.
- [ ] Cancellation.
- [ ] Provider failure.
- [ ] Partial input/output failure rollback.
- [ ] Process scheduling/state transitions.
- [ ] Snapshot/restore executions, reservations, generators, journal.

#### ResourcesProduction

Freeze unit: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction`. Admission API: 88, obligations: 84, lifecycle: 1, stale: 21, external: 3.

- [ ] Resource definitions/stores/producers.
- [ ] Production reservation.
- [ ] Consume/produce atomicity.
- [ ] Capacity and quantity boundaries.
- [ ] Negative/overflow quantities rejected.
- [ ] Producer lifecycle.
- [ ] Process integration ports remain external.
- [ ] Snapshot/restore.

Block exit:

- [ ] Все модули A08 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A09. RolesJobs, NeedsLife

Admission workload: 138 API, 152 mutation obligations, 6 lifecycle, 37 stale-identity, 0 external-boundary candidates, 2 public headers.

#### RolesJobs

Freeze unit: `EngineFramework/GameplayWorldStateOwners/RolesJobs`. Admission API: 73, obligations: 80, lifecycle: 5, stale: 20, external: 0.

- [ ] Role/job definitions.
- [ ] Assignment/unassignment.
- [ ] Capacity/eligibility rules.
- [ ] Worker/job stale references.
- [ ] Duplicate assignment.
- [ ] Job lifecycle.
- [ ] Failed reassignment leaves old assignment valid.
- [ ] Snapshot/restore.

#### NeedsLife

Freeze unit: `EngineFramework/GameplayWorldStateOwners/NeedsLife`. Admission API: 65, obligations: 72, lifecycle: 1, stale: 17, external: 0.

- [ ] Need definitions/state.
- [ ] Satisfy/decay boundaries.
- [ ] Life pressure lifecycle.
- [ ] Expiration sweep.
- [ ] Resolve pressure.
- [ ] Simulation interval including large delta.
- [ ] Terminal pressure pruning.
- [ ] No negative/overflow need values unless contract allows.
- [ ] Snapshot/restore.

Block exit:

- [ ] Все модули A09 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A10. Population, Encounters

Admission workload: 145 API, 148 mutation obligations, 4 lifecycle, 33 stale-identity, 0 external-boundary candidates, 2 public headers.

#### Population

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Population`. Admission API: 75, obligations: 80, lifecycle: 2, stale: 19, external: 0.

- [ ] Template registry and definition freeze.
- [ ] Group and unit creation; group counters and all unit indexes remain consistent.
- [ ] Unit semantic lifecycle: virtual/materialized, dematerialize, dead and retired states.
- [ ] Entity binding and stale entity/reference handling.
- [ ] Residence assignment and area/residence indexes.
- [ ] Migration lifecycle: start, complete, cancel and fail; no half-updated group/area state.
- [ ] Allocation lifecycle: reserve batch, commit, release and terminal pruning; active-allocation uniqueness.
- [ ] Capacity, aggregate counts, revision/change-sequence exhaustion and deterministic query ordering.
- [ ] Snapshot/restore of templates, groups, units, residences, migrations, allocations, generators, indexes and journal.

#### Encounters

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Encounters`. Admission API: 70, obligations: 68, lifecycle: 2, stale: 14, external: 0.

- [ ] Definition/spawn table registration.
- [ ] Encounter start/active/terminal lifecycle.
- [ ] Spawn request lifecycle.
- [ ] Spawn point state.
- [ ] Spawned entity record consistency.
- [ ] Respawn rules.
- [ ] Budget limits.
- [ ] Fail encounter.
- [ ] Despawn lifecycle.
- [ ] Terminal pruning.
- [ ] Deterministic spawn roll via explicit random source.
- [ ] Snapshot/restore.

Block exit:

- [ ] Все модули A10 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A11. Society, Crime

Admission workload: 162 API, 136 mutation obligations, 2 lifecycle, 33 stale-identity, 0 external-boundary candidates, 2 public headers.

Подтверждённые defects этого потока:

- `G4-SOC-001`: Society has mutators that ignore failed `Bump()` and can modify restored max-revision state without a revision advance. Место: `EngineFramework/GameplayWorldStateOwners/Society/src/society.cpp:328-355,376-423,466-525`.
- `G4-SOC-002`: Society generators, primary maps, indices and journal are committed across multiple fallible steps. Stage all publication before commit. Место: `EngineFramework/GameplayWorldStateOwners/Society/src/society.cpp:283-307,397-423,1035-1044`.

#### Society

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Society`. Admission API: 62, obligations: 56, lifecycle: 1, stale: 14, external: 0.

- [ ] Social groups/relations/reputation definitions.
- [ ] Relationship mutation boundaries.
- [ ] Membership lifecycle.
- [ ] Symmetric/asymmetric relationship semantics.
- [ ] Duplicate relation prevention.
- [ ] Social change journal.
- [ ] Snapshot/restore.

#### Crime

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Crime`. Admission API: 100, obligations: 80, lifecycle: 1, stale: 19, external: 0.

- [ ] Law/jurisdiction/authority registration.
- [ ] Crime candidate evaluation.
- [ ] Crime record lifecycle.
- [ ] Evidence and witness lifecycle.
- [ ] Proof state transitions.
- [ ] Bounty lifecycle.
- [ ] Response request lifecycle.
- [ ] Invalid jurisdiction/authority reference.
- [ ] Duplicate evidence/witness behavior.
- [ ] Snapshot/restore all legal records and journal.

Block exit:

- [ ] Все модули A11 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A12. Perception, Knowledge, NavigationSemantics

Admission workload: 181 API, 164 mutation obligations, 3 lifecycle, 39 stale-identity, 1 external-boundary candidates, 3 public headers.

Подтверждённые defects этого потока:

- `G4-PER-001`: Perception converts int64 coordinates to MSVC binary64 before subtraction; adjacent >2^53 positions can collapse. Compute exact deltas before floating conversion. Место: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:300-307,339-360`.
- `G4-PER-002`: Perception awareness timestamp advance uses floating multiplication even though the exact product is bounded by elapsed time; MSVC rounding can overshoot `now`. Место: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:930-970`.
- `G4-PER-003`: Perception saturates squared distance to int64 before sqrt, collapsing every distance above ~3.037e9 mm and allowing false in-range results. Место: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:300-336`.

#### Perception

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Perception`. Admission API: 72, obligations: 56, lifecycle: 1, stale: 13, external: 0.

- [ ] Sense, perceiver-profile and evaluator definition registration/freeze.
- [ ] Stimulus lifecycle and perceiver sampling/provider contracts.
- [ ] Observation and awareness lifecycle, duplicate/coalescing semantics and deterministic ordering.
- [ ] Built-in spatial evaluation: range, attenuation and FOV with exact handling of large int64 millimetre coordinates.
- [ ] Provider/evaluator failure leaves observations, indexes, revision and journal unchanged.
- [ ] Expiration, forgetting, suspicion/awareness decay and large catch-up intervals.
- [ ] Materialized/runtime-projection flags and stale subject/stimulus references.
- [ ] Revision/change-sequence exhaustion and bounded journal semantics.
- [ ] Snapshot/restore of definitions where applicable, stimuli, observations, awareness state, generators, indexes and journal.

#### Knowledge

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Knowledge`. Admission API: 40, obligations: 48, lifecycle: 1, stale: 12, external: 1.

- [ ] Knowledge learn/share/forget/contradict lifecycle.
- [ ] Confidence/value boundaries.
- [ ] Memory compaction.
- [ ] Subject/source references.
- [ ] Duplicate knowledge semantics.
- [ ] Contradiction does not corrupt indexes.
- [ ] Decay.
- [ ] Graph/index consistency.
- [ ] Snapshot/restore memories, relations, generators, journal.

#### NavigationSemantics

Freeze unit: `EngineFramework/GameplayWorldStateOwners/NavigationSemantics`. Admission API: 69, obligations: 60, lifecycle: 1, stale: 14, external: 0.

- [ ] Semantic area/layer/rule registration.
- [ ] Registry freeze.
- [ ] Capability provider failure.
- [ ] Route/query semantic constraints.
- [ ] Layer create/update/remove.
- [ ] Invalid world references.
- [ ] Deterministic rule resolution.
- [ ] Snapshot/restore semantic graph/indexes.

Block exit:

- [ ] Все модули A12 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A13. AI, Simulation

Admission workload: 135 API, 144 mutation obligations, 6 lifecycle, 35 stale-identity, 0 external-boundary candidates, 2 public headers.

#### AI

Freeze unit: `EngineFramework/GameplayWorldStateOwners/AI`. Admission API: 90, obligations: 104, lifecycle: 4, stale: 26, external: 0.

- [ ] Profile/goal/intent/consideration registration.
- [ ] Agent register/remove.
- [ ] Blackboard key/value type validation.
- [ ] Goal/intent lifecycle.
- [ ] Think/replan.
- [ ] Evaluator failure.
- [ ] Access policy.
- [ ] Target candidate handling.
- [ ] SetNextThink scheduling.
- [ ] Deterministic tie breaking.
- [ ] Failed think does not half-update agent/index/journal.
- [ ] Snapshot/restore agents, blackboards, intents, generators, scheduler state, journal.

#### Simulation

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Simulation`. Admission API: 45, obligations: 40, lifecycle: 2, stale: 9, external: 0.

- [ ] Framework simulation definitions/state.
- [ ] Job/task registration and lifecycle.
- [ ] Budgeted update.
- [ ] Proposal/application boundary to state owners.
- [ ] No duplicate authoritative state with Runtime Simulation.
- [ ] Large delta/catch-up.
- [ ] Failure atomicity.
- [ ] Snapshot/restore.

Block exit:

- [ ] Все модули A13 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A14. Combat, Loot

Admission workload: 104 API, 144 mutation obligations, 2 lifecycle, 32 stale-identity, 0 external-boundary candidates, 2 public headers.

Подтверждённые defects этого потока:

- `G4-COMBAT-001`: Combat proportional int64 scaling relies on `long double`. On MSVC the boundary case `M-1,M,M-1` has exact result `M-2`, while the current binary64 path rounds/clamps to `M`. Место: `EngineFramework/GameplayWorldStateOwners/Combat/src/combat.cpp:58-70,286-307`.

#### Combat

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Combat`. Admission API: 52, obligations: 80, lifecycle: 1, stale: 17, external: 0.

- [ ] Combatant registration/removal.
- [ ] Engagement lifecycle.
- [ ] Damage plan/resolve.
- [ ] Modifier ordering.
- [ ] Resource reservation.
- [ ] Resource depletion transitions alive/downed/dead/disabled.
- [ ] Clamp/overflow behavior.
- [ ] Modifier provider failure.
- [ ] Failed resolution does not consume reservation or change resource.
- [ ] Snapshot/restore combatants, reservations, generators, journal.
- [ ] Preserve-ratio SetResourceMaximum must use portable exact/safe integer ratio arithmetic near int64 boundaries; MSVC double precision may not decide the result.

#### Loot

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Loot`. Admission API: 52, obligations: 64, lifecycle: 1, stale: 15, external: 0.

- [ ] Loot table/definition registration.
- [ ] Generate deterministic output from explicit random source.
- [ ] Generated, pending, delivered, cancelled lifecycle.
- [ ] Reward handler prepare/commit/cancel.
- [ ] Discard generated.
- [ ] Duplicate delivery protection.
- [ ] External reward failure and reconciliation.
- [ ] Snapshot/restore pending deliveries, generators, journal.

Block exit:

- [ ] Все модули A14 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A15. Abilities, Progression

Admission workload: 120 API, 192 mutation obligations, 2 lifecycle, 48 stale-identity, 0 external-boundary candidates, 2 public headers.

Подтверждённые defects этого потока:

- `G4-PROG-001`: Progression `RemoveProfile()` erases authoritative state before revision-exhaustion preflight. Место: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:305-319`.
- `G4-PROG-002`: Progression has state mutations and reservation consumption before proving all revision/journal capacity; several `Bump()` failures are ignored. Место: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:372-384,747-799,803-905,1070-1097`.
- `G4-PROG-003`: Progression `AddModifier()` advances the live modifier generator before revision and allocation success. Место: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:462-492`.

#### Abilities

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Abilities`. Admission API: 57, obligations: 92, lifecycle: 1, stale: 23, external: 0.

- [ ] Definition/instance grant/remove.
- [ ] Availability checks.
- [ ] Activation lifecycle.
- [ ] Target policy.
- [ ] Cost reservation.
- [ ] Cooldown group.
- [ ] Requirement provider failure.
- [ ] Resource provider prepare/commit/release.
- [ ] Materialization provider failure.
- [ ] Execution cancel/complete.
- [ ] Duplicate delivery prevention.
- [ ] Snapshot/restore instances, executions, reservations, cooldowns, generators, journal.

#### Progression

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Progression`. Admission API: 63, obligations: 100, lifecycle: 1, stale: 25, external: 0.

- [ ] Attribute, track, perk, unlock and milestone definition registration plus freeze validation.
- [ ] Profile create/remove and base-attribute mutation.
- [ ] Modifier add/remove/replace-by-source and service-wide modifier identity.
- [ ] GrantProgress/SetProgress rank transitions and arithmetic boundaries.
- [ ] ReserveProgressGrant/CommitProgressGrant/ReleaseProgressGrant exact reservation semantics.
- [ ] Perk and unlock grant/revoke semantics plus milestone evaluation and reward deduplication.
- [ ] Revision and journal exhaustion must reject before mutation; no successful mutation without revision advance.
- [ ] Snapshot/restore of profiles, modifiers, tracks, perks, unlocks, milestones, generators and journal.

Block exit:

- [ ] Все модули A15 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A16. Construction, Traversal

Admission workload: 190 API, 220 mutation obligations, 8 lifecycle, 50 stale-identity, 0 external-boundary candidates, 2 public headers.

Подтверждённые defects этого потока:

- `G4-TRAV-001`: Traversal ignores failed revision bumps across capability/mode/route/session/carrier mutations; restored max revision can mutate with stale revision. Место: `EngineFramework/GameplayWorldStateOwners/Traversal/src/traversal.cpp:215-260,405-482,540-678`.
- `G4-TRAV-002`: Traversal performs fallible map/journal publication after revision/state changes. Session/state cross-links must be staged and committed together. Место: `EngineFramework/GameplayWorldStateOwners/Traversal/src/traversal.cpp:552-564,648-653,965-974`.

#### Construction

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Construction`. Admission API: 108, obligations: 128, lifecycle: 5, stale: 29, external: 0.

- [ ] Placement rules/recipes registration.
- [ ] Placement validation.
- [ ] Plan lifecycle.
- [ ] Cost reservation.
- [ ] Socket reservation.
- [ ] Site lifecycle.
- [ ] Commit/cancel placement.
- [ ] Output creation.
- [ ] Provider failure.
- [ ] Collision/conflicting socket rejection.
- [ ] Snapshot/restore plans, sites, reservations, generators, journal.

#### Traversal

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Traversal`. Admission API: 82, obligations: 92, lifecycle: 3, stale: 21, external: 0.

- [ ] Traversal capability/route/action definitions.
- [ ] Begin/progress/complete/cancel lifecycle.
- [ ] Capability validation.
- [ ] Cost/reservation semantics.
- [ ] Stale world/entity references.
- [ ] Failure leaves traversal state unchanged.
- [ ] Snapshot/restore.

Block exit:

- [ ] Все модули A16 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A17. World, SaveGame, WorldIntegration

Admission workload: 128 API, 152 mutation obligations, 2 lifecycle, 32 stale-identity, 1 external-boundary candidates, 3 public headers.

#### World

Freeze unit: `EngineFramework/GameplayWorldStateOwners/World`. Admission API: 77, obligations: 92, lifecycle: 1, stale: 22, external: 0.

- [ ] Feature/area/placement create/update/remove.
- [ ] Alteration lifecycle.
- [ ] Spatial index consistency.
- [ ] Transaction commit/cancel.
- [ ] Alteration ID generator staging.
- [ ] Invalid enum rejection.
- [ ] Large alteration index.
- [ ] Journal atomicity.
- [ ] Snapshot restore builds all indexes off-state.
- [ ] Failed restore leaves old world unchanged.

#### SaveGame

Freeze unit: `EngineFramework/GameplayWorldStateOwners/SaveGame`. Admission API: 35, obligations: 40, lifecycle: 1, stale: 6, external: 1.

- [ ] Barrier binding and capture/restore lease acquisition/release semantics.
- [ ] Participant registration, duplicate IDs, required/optional participants and registry freeze.
- [ ] Migration registration and deterministic dependency/order resolution.
- [ ] Capture produces an in-memory SaveGameImage with structural metadata and per-section hashes.
- [ ] Participant capture failure is surfaced without partial image publication.
- [ ] Image structure, format/version compatibility and section hash validation.
- [ ] Migration chain validation and migration failure before restore publication.
- [ ] Restore stages all participants first; every fallible operation must complete before noexcept CommitRestore begins.
- [ ] Participant validation/staging failure leaves live state unchanged; commit ordering follows resolved dependencies.
- [ ] Persistent/session separation and whole-framework image metadata.
- [ ] Do not add storage/file I/O or serializer responsibilities to this module in Goal 4; those are whole-engine Goal 5 concerns.

#### WorldIntegration

Freeze unit: `EngineFramework/IntegrationLayer/WorldIntegration`. Admission API: 16, obligations: 20, lifecycle: 0, stale: 4, external: 0.

- [ ] World query adapters.
- [ ] World -> Facts projection.
- [ ] Environment sample mapping.
- [ ] Entity -> Interaction state provider.
- [ ] Alteration create/update/remove projection.
- [ ] Duplicate world change.
- [ ] Stale cursor.
- [ ] Restore checkpoint and resync.

Block exit:

- [ ] Все модули A17 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A18. Narrative, NarrativeIntegration

Admission workload: 212 API, 184 mutation obligations, 5 lifecycle, 45 stale-identity, 0 external-boundary candidates, 2 public headers.

Подтверждённые defects этого потока:

- `G4-NARRINT-001`: Narrative external-consequence outbox can wrap restored max revision because `Bump()` is unchecked. Место: `EngineFramework/IntegrationLayer/NarrativeIntegration/include/Epidemic/GameFramework/NarrativeIntegration/narrative_adapters.h:164-183`.
- `G4-NARRINT-002`: Narrative outbox `Execute()` bumps revision before fallible delivery publication. Место: `EngineFramework/IntegrationLayer/NarrativeIntegration/src/narrative_adapters.cpp:551-595`.

#### Narrative

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Narrative`. Admission API: 169, obligations: 140, lifecycle: 4, stale: 34, external: 0.

- [ ] Thread lifecycle.
- [ ] Objective lifecycle.
- [ ] Clue discovery.
- [ ] Choice resolve/cancel.
- [ ] Consequence prepare/commit/failure.
- [ ] Suspend/resume/fail thread.
- [ ] Duplicate consequence delivery prevention.
- [ ] Terminal execution compaction.
- [ ] Cursors/checkpoints.
- [ ] Snapshot/restore threads, objectives, choices, deliveries, generators, journal.

#### NarrativeIntegration

Freeze unit: `EngineFramework/IntegrationLayer/NarrativeIntegration`. Admission API: 43, obligations: 44, lifecycle: 1, stale: 11, external: 0.

- [ ] Narrative semantic event mapping.
- [ ] Contract registry duplicate/freeze.
- [ ] Narrative external consequence outbox.
- [ ] Handler failure and retry.
- [ ] Save participant roundtrip.
- [ ] Knowledge -> Narrative reference.
- [ ] Duplicate external consequence prevention.
- [ ] External-consequence outbox revision exhaustion is a hard terminal boundary: no wrap to zero after restored max revision.
- [ ] Outbox Execute must not advance revision before a fallible delivery allocation/publication.

Block exit:

- [ ] Все модули A18 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A19. Dialogue, Integration, StateIntegration, InteractionTimeIntegration, InteractionEffectsIntegration

Admission workload: 170 API, 240 mutation obligations, 6 lifecycle, 51 stale-identity, 2 external-boundary candidates, 5 public headers.

#### Dialogue

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Dialogue`. Admission API: 65, obligations: 60, lifecycle: 4, stale: 15, external: 0.

- [ ] Conversation definition/session lifecycle.
- [ ] Participant validation.
- [ ] Condition resolver success/failure.
- [ ] Option repeat policy.
- [ ] Consequence handler success/failure.
- [ ] Node transition validity.
- [ ] Conversation terminal state.
- [ ] Duplicate consequence execution prevention.
- [ ] Snapshot/restore sessions and journal.

#### Integration

Freeze unit: `EngineFramework/IntegrationLayer/Integration`. Admission API: 34, obligations: 68, lifecycle: 1, stale: 14, external: 2.

- [ ] FactsQueryAdapter query mapping.
- [ ] RuntimeTimeAdapter time projection.
- [ ] ScheduledTriggerDispatcher registration/freeze.
- [ ] Trigger handler failure.
- [ ] Delivery modes.
- [ ] Duplicate trigger prevention.
- [ ] Dispatcher checkpoint/save participant.
- [ ] TimeFactsAdapter exactly-once event projection.

#### StateIntegration

Freeze unit: `EngineFramework/IntegrationLayer/StateIntegration`. Admission API: 52, obligations: 72, lifecycle: 0, stale: 13, external: 0.

- [ ] Entity/material/condition queries.
- [ ] State -> Facts projection.
- [ ] Entity create/convert/tag effects.
- [ ] Material stimulus/exposure effects.
- [ ] Condition apply/remove effect delivery.
- [ ] Entity target state provider.
- [ ] Lifecycle adapter.
- [ ] State <-> Time processing.
- [ ] Deferred effect reconciliation.
- [ ] Combined checkpoint and persistence.
- [ ] Duplicate event/effect protection.

#### InteractionTimeIntegration

Freeze unit: `EngineFramework/IntegrationLayer/InteractionTimeIntegration`. Admission API: 8, obligations: 16, lifecycle: 0, stale: 4, external: 0.

- [ ] Interaction -> scheduled time binding.
- [ ] Cancel interaction removes/invalidates schedule по контракту.
- [ ] Trigger after interaction terminal state.
- [ ] Duplicate schedule delivery.
- [ ] Reconciliation after restore.

#### InteractionEffectsIntegration

Freeze unit: `EngineFramework/IntegrationLayer/InteractionEffectsIntegration`. Admission API: 11, obligations: 24, lifecycle: 1, stale: 5, external: 0.

- [ ] Interaction completion -> Effects exactly once.
- [ ] Effect prepare failure.
- [ ] Effect commit failure.
- [ ] Reconciliation state.
- [ ] Duplicate interaction completion.
- [ ] Restore delivery records.

Block exit:

- [ ] Все модули A19 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

### A20. GameplayIntegration, ExtendedGameplayIntegration, PerceptionKnowledgeAIIntegration, PopulationSimulationIntegration, ProcessResourceSimulationIntegration, SocialLegalIntegration, TraversalNavigationConstructionIntegration

Admission workload: 200 API, 312 mutation obligations, 6 lifecycle, 75 stale-identity, 2 external-boundary candidates, 7 public headers.

Подтверждённые defects этого потока:

- `G4-EXTINT-001`: TradeCoordinator allocates a persisted execution ID before full goods validation; invalid input can consume an ID. Место: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/src/extended_gameplay_adapters.cpp:410-456`.
- `G4-EXTINT-002`: TradeCoordinator accepts external goods/money reservations before guaranteed durable local execution/reconciliation publication. Место: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/src/extended_gameplay_adapters.cpp:458-536`.

#### GameplayIntegration

Freeze unit: `EngineFramework/IntegrationLayer/GameplayIntegration`. Admission API: 72, obligations: 132, lifecycle: 5, stale: 31, external: 0.

- [ ] Combat -> Effects damage delivery.
- [ ] Progression -> Combat modifier mapping.
- [ ] Combat <-> Ability resource reservation.
- [ ] Ability -> Effects delivery.
- [ ] Conditions -> Progression.
- [ ] Ability -> Time scheduled trigger.
- [ ] Ability output coordinator.
- [ ] Loot -> Progression rewards.
- [ ] Death reward delivery.
- [ ] Prepare/commit/release failures на каждом внешнем leg.
- [ ] Checkpoint restore без duplicate rewards/effects.

#### ExtendedGameplayIntegration

Freeze unit: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration`. Admission API: 42, obligations: 68, lifecycle: 0, stale: 17, external: 1.

- [ ] Equipment <-> Items reservation/ownership.
- [ ] Processes <-> Items input/output.
- [ ] Dialogue -> Knowledge consequence.
- [ ] Economy/Items/Ownership coordinated trade.
- [ ] Second prepare failure rollback.
- [ ] Money leg committed, goods leg failed reconciliation.
- [ ] Goods leg committed, ownership leg failed reconciliation.
- [ ] Cancel path.
- [ ] Restore trade execution checkpoint.
- [ ] Повторный retry не дублирует transfer.

#### PerceptionKnowledgeAIIntegration

Freeze unit: `EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration`. Admission API: 14, obligations: 12, lifecycle: 0, stale: 2, external: 0.

- [ ] Observation -> Knowledge mapping.
- [ ] Duplicate observation.
- [ ] Knowledge update -> AI inputs.
- [ ] Missing knowledge.
- [ ] AI execution availability.
- [ ] Intent execution record exactly once.
- [ ] Stale observation/agent generation.

#### PopulationSimulationIntegration

Freeze unit: `EngineFramework/IntegrationLayer/PopulationSimulationIntegration`. Admission API: 14, obligations: 28, lifecycle: 0, stale: 7, external: 0.

- [ ] Population backed encounter planning.
- [ ] Spawn success/failure.
- [ ] Encounter termination updates population once.
- [ ] Roles -> Needs mapping.
- [ ] Population lifecycle reconciliation.
- [ ] City life scheduled update.
- [ ] Snapshot restore plans/checkpoints.

#### ProcessResourceSimulationIntegration

Freeze unit: `EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration`. Admission API: 22, obligations: 32, lifecycle: 0, stale: 8, external: 1.

- [ ] Processes <-> resource input reservation.
- [ ] Output prepare/commit.
- [ ] Provider failure.
- [ ] Resource quantity rollback.
- [ ] Simulation layer proposal/application.
- [ ] Duplicate simulation execution.
- [ ] Restore checkpoint/reconciliation.

#### SocialLegalIntegration

Freeze unit: `EngineFramework/IntegrationLayer/SocialLegalIntegration`. Admission API: 28, obligations: 40, lifecycle: 1, stale: 10, external: 0.

- [ ] Ownership -> Crime theft resolution.
- [ ] Crime -> Society relationship consequence.
- [ ] Authority response mapping.
- [ ] Duplicate crime input.
- [ ] Victim policy.
- [ ] Failed consequence delivery.
- [ ] Checkpoint restore без duplicate penalty.

#### TraversalNavigationConstructionIntegration

Freeze unit: `EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration`. Admission API: 8, obligations: 0, lifecycle: 0, stale: 0, external: 0.

- [ ] TraversalNavigationCapabilityProvider maps traversal capabilities into NavigationSemantics without owning either state.
- [ ] TraversalNavigationAdapter::CanUseLink propagates stale/invalid subject and capability decisions correctly.
- [ ] ConstructionNavigationLayerPayload encoding/decoding validates durable payloads and stable layer identity.
- [ ] ConstructionNavigationAdapter::QueueNavigationOperation creates durable placement outputs without duplicate semantic operations.
- [ ] ProcessPendingOutputs performs idempotent add/update/remove, acknowledges only after Navigation commit, and supports retry after partial external progress.
- [ ] Duplicate delivery is idempotent by stable layer ID; already-applied state is acknowledged rather than duplicated.
- [ ] ConstructionTraversalAdapter::CancelTraversalSessions handles the documented partial-progress/retry contract without claiming batch atomicity that the API does not provide.
- [ ] Corrupt payloads, stale world references and restore/replay paths leave both sides reconcilable.

Block exit:

- [ ] Все модули A20 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- [ ] Все подтверждённые defects блока исправлены и имеют regression path.
- [ ] Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- [ ] Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

## 6A. Инженерные паспорта всех 52 Framework modules

Этот раздел дополняет checklist A01-A20. Он нужен, чтобы worker видел не только тему аудита, но и конкретную рабочую поверхность, риски и минимальный regression pack. Если для модуля нет admission defect ID, это означает только «дефект ещё не доказан», а не «модуль уже готов».

### A01 engineering map

#### Foundation: рабочая карточка

Код: `EngineFramework/BaseInfrastructure/Foundation/include`, `EngineFramework/BaseInfrastructure/Foundation/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/foundation_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: registry freeze, deterministic queries/order, invalid input/no-op semantics, snapshot or persistent state where exposed.

Функциональная поверхность, которую нужно доказать/доработать:

- Все gameplay IDs, refs, tags и handles имеют однозначную invalid/stale semantics.
- IdGenerator snapshot/restore и exhaustion.
- ChangeCursor ordering и stale cursor behavior.
- TypeRegistry duplicate registration.
- TypeRegistry freeze.
- GameplayContext не создаёт hidden ownership.
- Gameplay time/value types boundary arithmetic.

Минимальный regression pack: registry duplicate/freeze; invalid/no-op; deterministic output; boundary/exhaustion where applicable.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### SupportRandom: рабочая карточка

Код: `EngineFramework/BaseInfrastructure/SupportRandom/include`, `EngineFramework/BaseInfrastructure/SupportRandom/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/random_support_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: deterministic sequence, seed derivation, exhaustion, snapshot/replay equivalence.

Функциональная поверхность, которую нужно доказать/доработать:

- Один seed и одна sequence дают одинаковые значения.
- Snapshot/restore random sequence продолжает тот же stream.
- Разные streams не делят hidden mutable state.
- Boundary ranges не имеют modulo bias, если contract обещает uniform result.
- Invalid range отклоняется.
- Randomness не зависит от wall clock после создания stream.
- Все модули A01 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: known-vector determinism; exhaustion; snapshot/replay; invalid bound/range.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A02 engineering map

#### Queries: рабочая карточка

Код: `EngineFramework/BaseInfrastructure/Queries/include`, `EngineFramework/BaseInfrastructure/Queries/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/queries_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: registry freeze, deterministic queries/order, invalid input/no-op semantics, snapshot or persistent state where exposed.

Функциональная поверхность, которую нужно доказать/доработать:

- Provider registration, duplicate и freeze.
- Query type mismatch.
- Consistency/coverage/accuracy requirements.
- Budget exhaustion.
- Snapshot coordinator epoch.
- Provider failure не оставляет partial response state.
- Deterministic provider order.
- Detached query response lifetime.
- Diagnostics не является authoritative state.

Минимальный regression pack: registry duplicate/freeze; invalid/no-op; deterministic output; boundary/exhaustion where applicable.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Facts: рабочая карточка

Код: `EngineFramework/BaseInfrastructure/Facts/include`, `EngineFramework/BaseInfrastructure/Facts/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/facts_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: transaction/event ordering, local/global sequences, subscriber isolation, persistence snapshot, payload codec boundaries.

Функциональная поверхность, которую нужно доказать/доработать:

- Fact assert/update/remove lifecycle.
- Event publication.
- History policy.
- Transaction commit/rollback.
- Duplicate fact/event identities.
- Type registration и freeze.
- Subscriber reentrancy.
- Subscriber failure policy.
- Journal/history cursor.
- Compaction не удаляет необходимый retained history.
- Snapshot/restore persistent facts, events, generator and cursor.
- Failed transaction или allocation не публикует half-state.

Минимальный regression pack: batch/event sequence exhaustion; failed commit atomicity; subscriber throw isolation; snapshot/persistence roundtrip; payload type/codec mismatch.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Time: рабочая карточка

Код: `EngineFramework/BaseInfrastructure/Time/include`, `EngineFramework/BaseInfrastructure/Time/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/time_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: timer/schedule identity, catch-up, duration arithmetic, cancellation, snapshot continuity.

Функциональная поверхность, которую нужно доказать/доработать:

- Clock registration/state.
- Schedule create/update/remove.
- Recurrence daily/calendar/custom boundaries.
- Catch-up policies.
- Large time jump.
- Duplicate schedule identity.
- Cancel terminal semantics.
- Trigger order deterministic при одинаковом timestamp.
- Scheduler budget exhaustion deferred, без потери triggers.
- Journal/cursor consistency.
- Snapshot/restore clocks, schedules, generators, cursor.
- Все модули A02 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.

Минимальный regression pack: timer exhaustion; cancellation/no-op; large catch-up; partitioned vs combined time; snapshot roundtrip.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A03 engineering map

#### RuntimeBridge: рабочая карточка

Код: `EngineFramework/RuntimeBoundary/RuntimeBridge/include`, `EngineFramework/RuntimeBoundary/RuntimeBridge/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/runtime_bridge_tests.cpp`

Подтверждённые admission defects: `G4-RB-001`

Главные риски: runtime↔semantic conversion, stale generation/handle, accepted Runtime work, numeric representability, retry/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Framework semantic object creates exactly one runtime materialization per binding generation.
- Runtime handle/generation forward and reverse maps remain mutually consistent.
- Create/update/remove projection commands and dematerialization cleanup are retry-safe.
- Runtime backend failure must not falsely publish semantic success.
- Stale runtime observation cannot apply to a newer Framework generation.
- Duplicate command/event delivery is idempotent or rejected by the documented contract.
- Queue ordering, coalescing, backpressure and processing budgets preserve accepted work.
- World/Physics/Environment/Navigation observations validate finite numeric data and semantic ranges.
- Visibility distance/direction math remains valid for every finite runtime position representable by the Runtime contract.
- Checkpoint/reconciliation state, if present, restores mappings without orphaned Runtime representations.
- Все модули A03 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.

Минимальный regression pack: create/update/remove retry; stale generation; backend failure; checkpoint/reconciliation; extreme finite numeric conversions; duplicate command/event.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A04 engineering map

#### Entities: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Entities/include`, `EngineFramework/GameplayWorldStateOwners/Entities/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/entities_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Entity create/remove/deactivate/convert lifecycle.
- Archetype and part references.
- Tags and parts indexes.
- Stale entity/part references.
- Convert сохраняет только разрешённые fields.
- Pruning history не повреждает latest cursor.
- Snapshot/restore records, indexes, generators, revisions, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Materials: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Materials/include`, `EngineFramework/GameplayWorldStateOwners/Materials/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/materials_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Material/substance/type registration.
- Composition validation и normalization.
- Slot assignment.
- Stimulus/exposure mutation.
- Cross-reference validity.
- Derived material response соответствует primary composition.
- Journal prune boundary.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Environment: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Environment/include`, `EngineFramework/GameplayWorldStateOwners/Environment/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/environment_gameplay_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Gameplay environment definitions and state.
- Region/area references.
- Weather/environment semantic mutation.
- Expiration/sweep.
- Derived queries match authoritative state.
- No duplicate semantic state against Runtime Environment.
- Snapshot/restore.
- Все модули A04 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A05 engineering map

#### Conditions: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Conditions/include`, `EngineFramework/GameplayWorldStateOwners/Conditions/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/conditions_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Definition registration и freeze.
- Apply/remove.
- Stacking policies.
- Persistence/materialization policies.
- Periodic catch-up.
- Expiration.
- Subject provider failure.
- Duplicate condition handling.
- Snapshot/restore instances, generators, revisions, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Effects: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Effects/include`, `EngineFramework/GameplayWorldStateOwners/Effects/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Definition/handler registration и freeze.
- Immediate execution.
- Deferred execution.
- Cancel deferred.
- Cancel deferred targeting.
- Take deferred by schedule.
- Multi-operation batch prepare/commit.
- Handler prepare failure.
- Handler commit failure.
- Target state provider failure.
- Execution budget.
- Journal pruning.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Interaction: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Interaction/include`, `EngineFramework/GameplayWorldStateOwners/Interaction/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/interaction_gameplay_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Interaction definition/session/request lifecycle.
- Start/advance/complete/cancel.
- Preconditions and state provider.
- Executor success/failure.
- Timeout/sweep.
- Duplicate execution prevention.
- Reservation/pending state rollback.
- Snapshot/restore active interactions and pending execution state.
- Все модули A05 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A06 engineering map

#### ItemsInventory: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/ItemsInventory/include`, `EngineFramework/GameplayWorldStateOwners/ItemsInventory/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/items_inventory_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Item definition and container registration.
- Item create/remove.
- Stack split/merge rules.
- Capacity and slot limits.
- Transfer within/between containers.
- Reservation create/consume/release.
- Prepare/commit/cancel transfer.
- Exchange reservations.
- Durability and charges boundaries.
- World/container bindings.
- Secondary container/item indexes.
- Failed transfer leaves source and target unchanged.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Equipment: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Equipment/include`, `EngineFramework/GameplayWorldStateOwners/Equipment/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/equipment_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Equipment slot definition.
- Equip/unequip.
- Prepare equip.
- Item provider reservation.
- Conflicting slots.
- Requirements and compatibility.
- Reconcile restored bindings.
- Exchange/reconcile reservations.
- Provider failure before and after prepare.
- Snapshot/restore equipment and external binding metadata.
- Все модули A06 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A07 engineering map

#### Ownership: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Ownership/include`, `EngineFramework/GameplayWorldStateOwners/Ownership/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/ownership_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Owner/property identity.
- Assign/transfer/remove ownership.
- Duplicate ownership prevention.
- Shared/fractional semantics, если предусмотрены.
- Stale owner/property references.
- Transfer atomicity.
- Journal/revision.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Economy: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Economy/include`, `EngineFramework/GameplayWorldStateOwners/Economy/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/economy_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Currency/account registration.
- Funds reservation lifecycle.
- Monetary transfer.
- Offer lifecycle.
- Trade transaction lifecycle.
- Debt and contract lifecycle.
- Price provider failure.
- No negative/overflow balance unless explicitly allowed.
- Reservation prevents double spending.
- Transfer failure leaves both accounts unchanged.
- Snapshot/restore accounts, reservations, offers, trades, debts, contracts, generators, journal.
- Все модули A07 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A08 engineering map

#### Processes: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Processes/include`, `EngineFramework/GameplayWorldStateOwners/Processes/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/processes_tests.cpp`

Подтверждённые admission defects: `G4-PROC-001`

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Process definition/execution lifecycle.
- Input reservations.
- Output prepare/commit.
- Cancellation.
- Provider failure.
- Partial input/output failure rollback.
- Process scheduling/state transitions.
- Snapshot/restore executions, reservations, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### ResourcesProduction: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/include`, `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/resources_production_tests.cpp`, `EngineFramework/DevelopmentInfrastructure/Tests/resources_production_separation_tests.cpp`

Подтверждённые admission defects: `G4-RESPROD-001`, `G4-RESPROD-002`

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Resource definitions/stores/producers.
- Production reservation.
- Consume/produce atomicity.
- Capacity and quantity boundaries.
- Negative/overflow quantities rejected.
- Producer lifecycle.
- Process integration ports remain external.
- Snapshot/restore.
- Все модули A08 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A09 engineering map

#### RolesJobs: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/RolesJobs/include`, `EngineFramework/GameplayWorldStateOwners/RolesJobs/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/roles_jobs_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Role/job definitions.
- Assignment/unassignment.
- Capacity/eligibility rules.
- Worker/job stale references.
- Duplicate assignment.
- Job lifecycle.
- Failed reassignment leaves old assignment valid.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### NeedsLife: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/NeedsLife/include`, `EngineFramework/GameplayWorldStateOwners/NeedsLife/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/needs_life_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Need definitions/state.
- Satisfy/decay boundaries.
- Life pressure lifecycle.
- Expiration sweep.
- Resolve pressure.
- Simulation interval including large delta.
- Terminal pressure pruning.
- No negative/overflow need values unless contract allows.
- Snapshot/restore.
- Все модули A09 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A10 engineering map

#### Population: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Population/include`, `EngineFramework/GameplayWorldStateOwners/Population/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/population_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Template registry and definition freeze.
- Group and unit creation; group counters and all unit indexes remain consistent.
- Unit semantic lifecycle: virtual/materialized, dematerialize, dead and retired states.
- Entity binding and stale entity/reference handling.
- Residence assignment and area/residence indexes.
- Migration lifecycle: start, complete, cancel and fail; no half-updated group/area state.
- Allocation lifecycle: reserve batch, commit, release and terminal pruning; active-allocation uniqueness.
- Capacity, aggregate counts, revision/change-sequence exhaustion and deterministic query ordering.
- Snapshot/restore of templates, groups, units, residences, migrations, allocations, generators, indexes and journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Encounters: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Encounters/include`, `EngineFramework/GameplayWorldStateOwners/Encounters/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/encounters_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Definition/spawn table registration.
- Encounter start/active/terminal lifecycle.
- Spawn request lifecycle.
- Spawn point state.
- Spawned entity record consistency.
- Respawn rules.
- Budget limits.
- Fail encounter.
- Despawn lifecycle.
- Terminal pruning.
- Deterministic spawn roll via explicit random source.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A11 engineering map

#### Society: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Society/include`, `EngineFramework/GameplayWorldStateOwners/Society/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/society_tests.cpp`

Подтверждённые admission defects: `G4-SOC-001`, `G4-SOC-002`

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Social groups/relations/reputation definitions.
- Relationship mutation boundaries.
- Membership lifecycle.
- Symmetric/asymmetric relationship semantics.
- Duplicate relation prevention.
- Social change journal.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Crime: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Crime/include`, `EngineFramework/GameplayWorldStateOwners/Crime/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/crime_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Law/jurisdiction/authority registration.
- Crime candidate evaluation.
- Crime record lifecycle.
- Evidence and witness lifecycle.
- Proof state transitions.
- Bounty lifecycle.
- Response request lifecycle.
- Invalid jurisdiction/authority reference.
- Duplicate evidence/witness behavior.
- Snapshot/restore all legal records and journal.
- Все модули A11 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A12 engineering map

#### Perception: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Perception/include`, `EngineFramework/GameplayWorldStateOwners/Perception/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/perception_tests.cpp`

Подтверждённые admission defects: `G4-PER-001`, `G4-PER-002`, `G4-PER-003`

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Sense, perceiver-profile and evaluator definition registration/freeze.
- Stimulus lifecycle and perceiver sampling/provider contracts.
- Observation and awareness lifecycle, duplicate/coalescing semantics and deterministic ordering.
- Built-in spatial evaluation: range, attenuation and FOV with exact handling of large int64 millimetre coordinates.
- Provider/evaluator failure leaves observations, indexes, revision and journal unchanged.
- Expiration, forgetting, suspicion/awareness decay and large catch-up intervals.
- Materialized/runtime-projection flags and stale subject/stimulus references.
- Revision/change-sequence exhaustion and bounded journal semantics.
- Snapshot/restore of definitions where applicable, stimuli, observations, awareness state, generators, indexes and journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Knowledge: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Knowledge/include`, `EngineFramework/GameplayWorldStateOwners/Knowledge/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/knowledge_memory_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Knowledge learn/share/forget/contradict lifecycle.
- Confidence/value boundaries.
- Memory compaction.
- Subject/source references.
- Duplicate knowledge semantics.
- Contradiction does not corrupt indexes.
- Decay.
- Graph/index consistency.
- Snapshot/restore memories, relations, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### NavigationSemantics: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/NavigationSemantics/include`, `EngineFramework/GameplayWorldStateOwners/NavigationSemantics/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/navigation_semantics_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Semantic area/layer/rule registration.
- Registry freeze.
- Capability provider failure.
- Route/query semantic constraints.
- Layer create/update/remove.
- Invalid world references.
- Deterministic rule resolution.
- Snapshot/restore semantic graph/indexes.
- Все модули A12 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A13 engineering map

#### AI: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/AI/include`, `EngineFramework/GameplayWorldStateOwners/AI/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/ai_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Profile/goal/intent/consideration registration.
- Agent register/remove.
- Blackboard key/value type validation.
- Goal/intent lifecycle.
- Think/replan.
- Evaluator failure.
- Access policy.
- Target candidate handling.
- SetNextThink scheduling.
- Deterministic tie breaking.
- Failed think does not half-update agent/index/journal.
- Snapshot/restore agents, blackboards, intents, generators, scheduler state, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Simulation: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Simulation/include`, `EngineFramework/GameplayWorldStateOwners/Simulation/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/simulation_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Framework simulation definitions/state.
- Job/task registration and lifecycle.
- Budgeted update.
- Proposal/application boundary to state owners.
- No duplicate authoritative state with Runtime Simulation.
- Large delta/catch-up.
- Failure atomicity.
- Snapshot/restore.
- Все модули A13 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A14 engineering map

#### Combat: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Combat/include`, `EngineFramework/GameplayWorldStateOwners/Combat/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/combat_tests.cpp`

Подтверждённые admission defects: `G4-COMBAT-001`

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Combatant registration/removal.
- Engagement lifecycle.
- Damage plan/resolve.
- Modifier ordering.
- Resource reservation.
- Resource depletion transitions alive/downed/dead/disabled.
- Clamp/overflow behavior.
- Modifier provider failure.
- Failed resolution does not consume reservation or change resource.
- Snapshot/restore combatants, reservations, generators, journal.
- Preserve-ratio SetResourceMaximum must use portable exact/safe integer ratio arithmetic near int64 boundaries; MSVC double precision may not decide the result.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Loot: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Loot/include`, `EngineFramework/GameplayWorldStateOwners/Loot/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/loot_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Loot table/definition registration.
- Generate deterministic output from explicit random source.
- Generated, pending, delivered, cancelled lifecycle.
- Reward handler prepare/commit/cancel.
- Discard generated.
- Duplicate delivery protection.
- External reward failure and reconciliation.
- Snapshot/restore pending deliveries, generators, journal.
- Все модули A14 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A15 engineering map

#### Abilities: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Abilities/include`, `EngineFramework/GameplayWorldStateOwners/Abilities/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/abilities_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Definition/instance grant/remove.
- Availability checks.
- Activation lifecycle.
- Target policy.
- Cost reservation.
- Cooldown group.
- Requirement provider failure.
- Resource provider prepare/commit/release.
- Materialization provider failure.
- Execution cancel/complete.
- Duplicate delivery prevention.
- Snapshot/restore instances, executions, reservations, cooldowns, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Progression: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Progression/include`, `EngineFramework/GameplayWorldStateOwners/Progression/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/progression_tests.cpp`

Подтверждённые admission defects: `G4-PROG-001`, `G4-PROG-002`, `G4-PROG-003`

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Attribute, track, perk, unlock and milestone definition registration plus freeze validation.
- Profile create/remove and base-attribute mutation.
- Modifier add/remove/replace-by-source and service-wide modifier identity.
- GrantProgress/SetProgress rank transitions and arithmetic boundaries.
- ReserveProgressGrant/CommitProgressGrant/ReleaseProgressGrant exact reservation semantics.
- Perk and unlock grant/revoke semantics plus milestone evaluation and reward deduplication.
- Revision and journal exhaustion must reject before mutation; no successful mutation without revision advance.
- Snapshot/restore of profiles, modifiers, tracks, perks, unlocks, milestones, generators and journal.
- Все модули A15 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A16 engineering map

#### Construction: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Construction/include`, `EngineFramework/GameplayWorldStateOwners/Construction/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/construction_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Placement rules/recipes registration.
- Placement validation.
- Plan lifecycle.
- Cost reservation.
- Socket reservation.
- Site lifecycle.
- Commit/cancel placement.
- Output creation.
- Provider failure.
- Collision/conflicting socket rejection.
- Snapshot/restore plans, sites, reservations, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Traversal: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Traversal/include`, `EngineFramework/GameplayWorldStateOwners/Traversal/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/traversal_tests.cpp`

Подтверждённые admission defects: `G4-TRAV-001`, `G4-TRAV-002`

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Traversal capability/route/action definitions.
- Begin/progress/complete/cancel lifecycle.
- Capability validation.
- Cost/reservation semantics.
- Stale world/entity references.
- Failure leaves traversal state unchanged.
- Snapshot/restore.
- Все модули A16 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A17 engineering map

#### World: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/World/include`, `EngineFramework/GameplayWorldStateOwners/World/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/world_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Feature/area/placement create/update/remove.
- Alteration lifecycle.
- Spatial index consistency.
- Transaction commit/cancel.
- Alteration ID generator staging.
- Invalid enum rejection.
- Large alteration index.
- Journal atomicity.
- Snapshot restore builds all indexes off-state.
- Failed restore leaves old world unchanged.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### SaveGame: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/SaveGame/include`, `EngineFramework/GameplayWorldStateOwners/SaveGame/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/save_game_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Barrier binding and capture/restore lease acquisition/release semantics.
- Participant registration, duplicate IDs, required/optional participants and registry freeze.
- Migration registration and deterministic dependency/order resolution.
- Capture produces an in-memory SaveGameImage with structural metadata and per-section hashes.
- Participant capture failure is surfaced without partial image publication.
- Image structure, format/version compatibility and section hash validation.
- Migration chain validation and migration failure before restore publication.
- Restore stages all participants first; every fallible operation must complete before noexcept CommitRestore begins.
- Participant validation/staging failure leaves live state unchanged; commit ordering follows resolved dependencies.
- Persistent/session separation and whole-framework image metadata.
- Do not add storage/file I/O or serializer responsibilities to this module in Goal 4; those are whole-engine Goal 5 concerns.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### WorldIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/WorldIntegration/include`, `EngineFramework/IntegrationLayer/WorldIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/world_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- World query adapters.
- World -> Facts projection.
- Environment sample mapping.
- Entity -> Interaction state provider.
- Alteration create/update/remove projection.
- Duplicate world change.
- Stale cursor.
- Restore checkpoint and resync.
- Все модули A17 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A18 engineering map

#### Narrative: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Narrative/include`, `EngineFramework/GameplayWorldStateOwners/Narrative/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/narrative_tests.cpp`, `EngineFramework/DevelopmentInfrastructure/Tests/narrative_choices_storylets_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Thread lifecycle.
- Objective lifecycle.
- Clue discovery.
- Choice resolve/cancel.
- Consequence prepare/commit/failure.
- Suspend/resume/fail thread.
- Duplicate consequence delivery prevention.
- Terminal execution compaction.
- Cursors/checkpoints.
- Snapshot/restore threads, objectives, choices, deliveries, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### NarrativeIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/NarrativeIntegration/include`, `EngineFramework/IntegrationLayer/NarrativeIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/narrative_integration_tests.cpp`

Подтверждённые admission defects: `G4-NARRINT-001`, `G4-NARRINT-002`

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Narrative semantic event mapping.
- Contract registry duplicate/freeze.
- Narrative external consequence outbox.
- Handler failure and retry.
- Save participant roundtrip.
- Knowledge -> Narrative reference.
- Duplicate external consequence prevention.
- External-consequence outbox revision exhaustion is a hard terminal boundary: no wrap to zero after restored max revision.
- Outbox Execute must not advance revision before a fallible delivery allocation/publication.
- Все модули A18 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A19 engineering map

#### Dialogue: рабочая карточка

Код: `EngineFramework/GameplayWorldStateOwners/Dialogue/include`, `EngineFramework/GameplayWorldStateOwners/Dialogue/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/dialogue_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Conversation definition/session lifecycle.
- Participant validation.
- Condition resolver success/failure.
- Option repeat policy.
- Consequence handler success/failure.
- Node transition validity.
- Conversation terminal state.
- Duplicate consequence execution prevention.
- Snapshot/restore sessions and journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### Integration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/Integration/include`, `EngineFramework/IntegrationLayer/Integration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/core_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- FactsQueryAdapter query mapping.
- RuntimeTimeAdapter time projection.
- ScheduledTriggerDispatcher registration/freeze.
- Trigger handler failure.
- Delivery modes.
- Duplicate trigger prevention.
- Dispatcher checkpoint/save participant.
- TimeFactsAdapter exactly-once event projection.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### StateIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/StateIntegration/include`, `EngineFramework/IntegrationLayer/StateIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/state_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Entity/material/condition queries.
- State -> Facts projection.
- Entity create/convert/tag effects.
- Material stimulus/exposure effects.
- Condition apply/remove effect delivery.
- Entity target state provider.
- Lifecycle adapter.
- State <-> Time processing.
- Deferred effect reconciliation.
- Combined checkpoint and persistence.
- Duplicate event/effect protection.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### InteractionTimeIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/InteractionTimeIntegration/include`, `EngineFramework/IntegrationLayer/InteractionTimeIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/interaction_time_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Interaction -> scheduled time binding.
- Cancel interaction removes/invalidates schedule по контракту.
- Trigger after interaction terminal state.
- Duplicate schedule delivery.
- Reconciliation after restore.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### InteractionEffectsIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/InteractionEffectsIntegration/include`, `EngineFramework/IntegrationLayer/InteractionEffectsIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/interaction_effects_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Interaction completion -> Effects exactly once.
- Effect prepare failure.
- Effect commit failure.
- Reconciliation state.
- Duplicate interaction completion.
- Restore delivery records.
- Все модули A19 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

### A20 engineering map

#### GameplayIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/GameplayIntegration/include`, `EngineFramework/IntegrationLayer/GameplayIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/gameplay_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Combat -> Effects damage delivery.
- Progression -> Combat modifier mapping.
- Combat <-> Ability resource reservation.
- Ability -> Effects delivery.
- Conditions -> Progression.
- Ability -> Time scheduled trigger.
- Ability output coordinator.
- Loot -> Progression rewards.
- Death reward delivery.
- Prepare/commit/release failures на каждом внешнем leg.
- Checkpoint restore без duplicate rewards/effects.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### ExtendedGameplayIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/include`, `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/extended_gameplay_integration_tests.cpp`

Подтверждённые admission defects: `G4-EXTINT-001`, `G4-EXTINT-002`

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Equipment <-> Items reservation/ownership.
- Processes <-> Items input/output.
- Dialogue -> Knowledge consequence.
- Economy/Items/Ownership coordinated trade.
- Second prepare failure rollback.
- Money leg committed, goods leg failed reconciliation.
- Goods leg committed, ownership leg failed reconciliation.
- Cancel path.
- Restore trade execution checkpoint.
- Повторный retry не дублирует transfer.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### PerceptionKnowledgeAIIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration/include`, `EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/perception_knowledge_ai_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Observation -> Knowledge mapping.
- Duplicate observation.
- Knowledge update -> AI inputs.
- Missing knowledge.
- AI execution availability.
- Intent execution record exactly once.
- Stale observation/agent generation.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### PopulationSimulationIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/PopulationSimulationIntegration/include`, `EngineFramework/IntegrationLayer/PopulationSimulationIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/population_simulation_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Population backed encounter planning.
- Spawn success/failure.
- Encounter termination updates population once.
- Roles -> Needs mapping.
- Population lifecycle reconciliation.
- City life scheduled update.
- Snapshot restore plans/checkpoints.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### ProcessResourceSimulationIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration/include`, `EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/process_resource_simulation_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Processes <-> resource input reservation.
- Output prepare/commit.
- Provider failure.
- Resource quantity rollback.
- Simulation layer proposal/application.
- Duplicate simulation execution.
- Restore checkpoint/reconciliation.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### SocialLegalIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/SocialLegalIntegration/include`, `EngineFramework/IntegrationLayer/SocialLegalIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/social_legal_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Ownership -> Crime theft resolution.
- Crime -> Society relationship consequence.
- Authority response mapping.
- Duplicate crime input.
- Victim policy.
- Failed consequence delivery.
- Checkpoint restore без duplicate penalty.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

#### TraversalNavigationConstructionIntegration: рабочая карточка

Код: `EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration/include`, `EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/traversal_navigation_construction_integration_tests.cpp`

Подтверждённые admission defects: не найдено. Worker не должен придумывать defect без reproduction.

Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- TraversalNavigationCapabilityProvider maps traversal capabilities into NavigationSemantics without owning either state.
- TraversalNavigationAdapter::CanUseLink propagates stale/invalid subject and capability decisions correctly.
- ConstructionNavigationLayerPayload encoding/decoding validates durable payloads and stable layer identity.
- ConstructionNavigationAdapter::QueueNavigationOperation creates durable placement outputs without duplicate semantic operations.
- ProcessPendingOutputs performs idempotent add/update/remove, acknowledges only after Navigation commit, and supports retry after partial external progress.
- Duplicate delivery is idempotent by stable layer ID; already-applied state is acknowledged rather than duplicated.
- ConstructionTraversalAdapter::CancelTraversalSessions handles the documented partial-progress/retry contract without claiming batch atomicity that the API does not provide.
- Corrupt payloads, stale world references and restore/replay paths leave both sides reconcilable.
- Все модули A20 имеют полный local evidence packet и не содержат `NOT_AUDITED` в block-local projection.
- Все подтверждённые defects блока исправлены и имеют regression path.
- Module tests Debug/Release проходят после изменений блока; warnings-as-errors сохранены.
- Изменения не заходят в ownership других Axx blocks и не редактируют canonical freeze registries.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

## 7. Как выполнить тот же план в 16 потоках

20 потоков предпочтительны. Если одновременно доступны только 16 workers, ownership фиксируется до старта и четыре пары объединяются:

- `A01 + A02`;
- `A03 + A14`;
- `A09 + A10`;
- `A13 + A17`.

`A19` и `A20` **не объединять**: это самые широкие IntegrationLayer blocks по obligation surface. После начала работ нельзя передавать production directory другому worker без явного handoff, иначе будет сложно доказать provenance fixes/evidence.

## 8. Serial convergence после A01-A20

После merge двадцати ownership deltas один integrator:

1. Проверяет, что все production directories имеют ровно одного provenance owner и shared preflight не откатан.
2. Проверяет repository-wide, что Goal 4 fault evidence больше не зависит от process-global `operator new/new[]/delete` и нет `_ITERATOR_DEBUG_LEVEL`/size-based bypass.
3. Объединяет `dossier_reviews`, coverage decisions, exact public API anchors и defect records в canonical source registries.
4. Запускает штатную регенерацию `module_dossiers`, `coverage_manifests`, `public_api_inventory`, `public_surface_manifest`, `local_ready_contract`.
5. Требует `52/52 EngineFramework = LOCAL_READY`, каждый модуль 37/37 `PASS/N/A`, все 15 dossier fields reviewed.
6. Требует exact Framework callable count после всех осознанных surface changes. Исходный admission count 3054 является baseline, а не числом, которое надо искусственно сохранить. Любое добавление/удаление public API получает отдельный review.
7. Все mutation obligations, lifecycle, stale-identity и external-boundary candidates становятся reviewed decisions. Массовое `N/A` без per-item rationale запрещено.
8. Каждый confirmed defect имеет regression target и вызываемый regression symbol. Defect без reproduction/evidence не переносится в canonical registry.
9. Сравнивает regenerated public surface с admission baseline. Необъяснённый header/API delta блокирует closure.
10. Обновляет 52 Framework module docs на фактический `LOCAL_READY`, но не `FROZEN`.

## 9. Финальный Goal 4 qualification gate

На одном recorded SHA после serial convergence:

- [ ] Base Debug/Release, Runtime Debug/Release и Full Debug/Release green; exact CTest manifests совпадают.
- [ ] Все Framework module/integration targets green; disabled/skipped без explicit freeze exception = 0.
- [ ] MSVC `/W4 /WX` green.
- [ ] MSVC public-header self-containment green.
- [ ] Все architecture/freeze validators и validator self-tests green.
- [ ] Remote Architecture Freeze CI на том же SHA green, включая ClangCL public-surface.
- [ ] `git diff --check` clean.
- [ ] `EngineBase = 9/9 LOCAL_READY`, `EngineRuntime = 17/17 LOCAL_READY`, `EngineFramework = 52/52 LOCAL_READY`.
- [ ] 52/52 Framework modules: 37/37 `PASS/N/A`, 15/15 dossier fields reviewed.
- [ ] Framework public API: `UNCLASSIFIED = 0`, contract/test anchors complete.
- [ ] Framework mutation obligations/lifecycle/stale/external candidates: unresolved = 0.
- [ ] Goal 4 defect registry: каждый entry имеет существующий target + вызываемый regression symbol.
- [ ] Process-global allocator fault override in Framework Goal 4 tests: 0.
- [ ] Module docs describe `LOCAL_READY` and explicitly reserve `FROZEN` for Goal 9.

Критерий выхода Goal 4: все 52 Framework freeze units локально доказаны без module-local code/evidence debt. Whole-engine persistence, concurrency/lifetime, real causal chains, load/degradation и final freeze остаются Goals 5-9.

## 10. Что было исправлено относительно старого Goal 4 текста

- SaveGame приведён к фактическому in-memory orchestration API; storage/serialization/read I/O удалены из Goal 4.
- Perception приведён к фактическим senses/profiles/perceivers/stimuli/observations/awareness API вместо несуществующей схемы observer/source/target registry.
- Population приведён к template/group/unit/residence/migration/allocation lifecycle вместо расплывчатого spawn/despawn описания.
- Progression приведён к attributes/tracks/modifiers/perks/unlocks/milestones и Reserve/Commit/Release, без несуществующего generic cancel API.
- TraversalNavigationConstructionIntegration переписан вокруг реальных durable placement outputs, stable layer IDs, `ProcessPendingOutputs`, acknowledgement-after-commit и retry/idempotence.
- IntegrationLayer остаётся fake-based local audit. Реальные causal chains не дублируются здесь и остаются Goal 7.
- Framework docs gap и unsafe global allocation injector повышены до preflight blockers, поскольку без них нельзя надёжно принимать module-local evidence.

## 11. Проверка соответствия всего `work-plan.md` текущему проекту

Goals 1-3 соответствуют текущему дереву после обновления статуса Goal 3 на `COMPLETE`. Старый раздел 0 был устаревшим и должен отражать рабочие architecture gates, 4311 total callables и завершённые Base/Runtime admissions.

Goals 5-9 по предметной области остаются актуальны и не требуют структурной перестройки: Goal 5 whole-engine persistence/determinism, Goal 6 lifetime/concurrency, Goal 7 causal clusters, Goal 8 fault/load/degradation, Goal 9 final saturation/freeze. В Goal 4 запрещено закрывать их требования заранее только ради повышения локального статуса Framework.

## 12. Admission conclusion

Повторный аудит подтвердил полноту module ownership: все 52 production-модуля входят ровно в один из A01-A20. Ни один BaseInfrastructure, RuntimeBoundary, GameplayWorldStateOwner или IntegrationLayer module не выпал.

Admission baseline пока не `LOCAL_READY`: 52/52 Framework modules остаются `IN_AUDIT`. Подтверждено **21** конкретных Goal 4 problem/blocker IDs, из них **2 shared** и **19 production/integration**. В первой версии плана часть этих проблем отсутствовала, поэтому текущая версия заменяет её как рабочий документ.

Главный принцип выполнения: A01-A20 не должны «переписать Framework». Они должны доказать текущий contract, исправить только воспроизводимые нарушения, закрепить их regressions и передать полный evidence packet serial integrator.

После выполнения preflight, A01-A20, serial convergence и финального qualification gate Goal 4 может быть закрыт без переноса локального Framework-долга в Goal 5.
