# RolesJobs Goal 4 local audit

## Scope

Freeze unit: `EngineFramework/GameplayWorldStateOwners/RolesJobs`. Admission: 73 public callables, 80 mutation obligations, 5 lifecycle candidates, 20 stale-identity candidates, 0 external-boundary candidates.

## Public contract review

- Role/job definitions and definition freeze semantics.
- Assignment/unassignment, duplicate assignment, capacity and eligibility.
- Workplace/job/duty lifecycle including forbidden transitions.
- Worker/job stale references and deterministic queries.
- Failed reassignment and restore are failure-atomic.
- Snapshot/restore preserves IDs, revisions, indexes and journal epoch/cursor.

## Failure atomicity and boundary review

All revision-bearing mutations were reviewed for revision/generator exhaustion before authoritative mutation. Multi-container mutations were reviewed for rollback or staged publication. Snapshot restore builds/validates candidate state before live-state replacement. Allocation-failure evidence for this block no longer depends on the process-global Framework allocator helper; owned tests use module-local failure seams where allocation failure is evidence.

## Persistence and deterministic reads

The module snapshot surface is treated as local in-memory persistence evidence for Goal 4. Non-empty roundtrip, invalid restore, generator/revision continuity, journal epoch/cursor behavior and failed-restore pre-state preservation are covered by the module test executable. Whole-engine ordered restore and replay remain Goal 5.

## Dossier review

- Responsibility: REVIEWED.
- Dependency list: REVIEWED.
- Public headers and types: REVIEWED.
- Public mutation API: REVIEWED.
- Read/query API for invariants: REVIEWED.
- Authoritative state: REVIEWED.
- Derived/cache/index state: REVIEWED.
- ID spaces, generations, revisions and cursors: REVIEWED.
- State machines: REVIEWED.
- Local invariants: REVIEWED.
- Persistent and transient state: REVIEWED.
- Snapshot/restore contract: REVIEWED.
- External ports/callbacks/providers/backends: REVIEWED.
- Hard limits, budgets and complexity bounds: REVIEWED.
- Threading contract: REVIEWED.

## Test evidence

Primary regression source: `EngineFramework/DevelopmentInfrastructure/Tests/roles_jobs_tests.cpp`.
Registered target: `EpidemicGameFrameworkRolesJobsTests`.
Local Linux verification in the worker environment used direct C++23 compilation with `-Wall -Wextra -Wpedantic -Werror` and executed the module test binary. The repository top-level CMake configure is Windows-only and therefore cannot be used in this Linux worker environment.

## Goal 4 result

Block-local review result: LOCAL_READY candidate for serial integration. This document does not claim whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN`.

## Exact public API anchors

Each row is the exact reviewed contract for one B04 callable. The matching assertion is recorded in `_goal4_handoff/B04/public_api_anchors.json`.

- `07ade6805ccebd74` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<DutyId> CreateDuty(Duty duty,GameplayContext context={});`
- `08b4336667a763aa` | `FACTORY` | `epidemic::gameplay::roles_jobs::RoleFunctionId` | `static constexpr RoleFunctionId FromString(std::string_view s)noexcept`
- `0da6cf7174dd7d66` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] std::uint64_t OldestChangeSequence()const noexcept;`
- `0dff8e5065924de2` | `FACTORY` | `epidemic::gameplay::roles_jobs::JobDefinitionId` | `static constexpr JobDefinitionId FromString(std::string_view s)noexcept`
- `100c7c370852815d` | `QUERY` | `epidemic::gameplay::roles_jobs::JobDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const JobDefinitionId&)const noexcept=default;`
- `1227dd83c0260a6e` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkplaceId` | `[[nodiscard]] constexpr auto operator<=>(const WorkplaceId&)const noexcept=default;`
- `15392ffeb35da58e` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] RolesJobsDiagnostics GetDiagnostics()const noexcept;`
- `1dd826a872c6e7cb` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> SuspendAssignment(JobAssignmentId id,GameplayContext context={});`
- `20b0ca640726be80` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] std::vector<Duty> FindDutiesForAssignment(JobAssignmentId assignment)const;`
- `21f7a704f539864a` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkShiftId` | `[[nodiscard]] constexpr bool operator==(const WorkShiftId&)const noexcept=default;`
- `2239bb2cb4b7b33f` | `QUERY` | `epidemic::gameplay::roles_jobs::DutyId` | `[[nodiscard]] constexpr bool operator==(const DutyId&)const noexcept=default;`
- `2820d2101cd2214f` | `QUERY` | `epidemic::gameplay::roles_jobs::RoleFunctionId` | `[[nodiscard]] constexpr bool operator==(const RoleFunctionId&)const noexcept=default;`
- `2b893cdab624130d` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> RegisterJobDefinition(JobDefinition definition);`
- `3523bec7e771111e` | `QUERY` | `epidemic::gameplay::roles_jobs::JobDefinitionId` | `[[nodiscard]] constexpr bool operator==(const JobDefinitionId&)const noexcept=default;`
- `3575dfce83540b35` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> FailDuty(DutyId id,GameplayContext context={});`
- `3854e497476d5422` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `3931483b2617ff0f` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] std::vector<JobAssignment> FindWorkersAtWorkplace(WorkplaceId workplace)const;`
- `3ca1e8d97134eeb9` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] RolesJobsSnapshot CaptureSnapshot()const;`
- `405def01d31fbc0a` | `QUERY` | `epidemic::gameplay::roles_jobs::RoleFunctionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `40604b78e74b8773` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkplaceId` | `[[nodiscard]] constexpr bool operator==(const WorkplaceId&)const noexcept=default;`
- `45ada65bbc3e1f2d` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> SetWorkplaceState(WorkplaceId id,WorkplaceState state,GameplayContext context={});`
- `486e86b6d6a6525f` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkplaceId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4c6330533ce5ad3f` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] std::vector<JobAssignment> FindJobsOfSubject(GameplayObjectRef subject)const;`
- `4d1f24789c4ad3bc` | `FACTORY` | `epidemic::gameplay::roles_jobs::WorkShiftId` | `static constexpr WorkShiftId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `4e53c1c6eec2921f` | `QUERY` | `epidemic::gameplay::roles_jobs::JobTaskTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4f7c437c7f011b0e` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `4f9ddf665492d0d2` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<WorkScheduleId> CreateSchedule(WorkSchedule schedule);`
- `591794bbd2b871a6` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> SkipDuty(DutyId id,GameplayContext context={});`
- `5aacfdbda9dd3f70` | `FACTORY` | `epidemic::gameplay::roles_jobs::WorkplaceId` | `static constexpr WorkplaceId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `600f63b6b49dcc65` | `QUERY` | `epidemic::gameplay::roles_jobs::RoleFunctionId` | `[[nodiscard]] constexpr auto operator<=>(const RoleFunctionId&)const noexcept=default;`
- `612f3f3dff19f80b` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<WorkplaceId> CreateWorkplace(Workplace workplace);`
- `64619970ecf6c43f` | `QUERY` | `epidemic::gameplay::roles_jobs::JobTaskTypeId` | `[[nodiscard]] constexpr auto operator<=>(const JobTaskTypeId&)const noexcept=default;`
- `6ebf212ef3fc694d` | `LIFECYCLE` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> ActivateDuty(DutyId id,GameplayContext context={});`
- `6fa4eefa826cd6e4` | `QUERY` | `epidemic::gameplay::roles_jobs::DutyId` | `[[nodiscard]] constexpr auto operator<=>(const DutyId&)const noexcept=default;`
- `73a42c988ceddd84` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] const Duty*GetCurrentDuty(GameplayObjectRef subject)const noexcept;`
- `747a26690eb0c402` | `QUERY` | `epidemic::gameplay::roles_jobs::JobDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `864a926ba4a08b7e` | `LIFECYCLE` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> PauseAssignment(JobAssignmentId id,GameplayContext context={});`
- `89e17fd3263285f3` | `FACTORY` | `epidemic::gameplay::roles_jobs::WorkShiftId` | `static constexpr WorkShiftId FromString(std::string_view s)noexcept`
- `8db534162ebacbb3` | `FACTORY` | `epidemic::gameplay::roles_jobs::DutyId` | `static constexpr DutyId FromString(std::string_view s)noexcept`
- `9799a24385d022dc` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<JobAssignmentId> AssignJob(JobAssignment assignment,GameplayContext context={});`
- `99a1a90cbbf20c20` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkScheduleId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `9ea76809bf419dd7` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> CompleteAssignment(JobAssignmentId id,GameplayContext context={});`
- `a239b10b974e428c` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] RolesJobsChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `a3ac89a6cee41dff` | `FACTORY` | `epidemic::gameplay::roles_jobs::WorkplaceId` | `static constexpr WorkplaceId FromString(std::string_view s)noexcept`
- `a5fb8af968fa45cb` | `FACTORY` | `epidemic::gameplay::roles_jobs::JobAssignmentId` | `static constexpr JobAssignmentId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `a8890a31bf809ff5` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] const Workplace*GetWorkplace(WorkplaceId id)const noexcept;`
- `ab4d17dd56808b8c` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `ab828fd75e8f0dda` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] const Duty*FindDuty(DutyId id)const noexcept;`
- `acc894332b24c175` | `LIFECYCLE` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> ResumeAssignment(JobAssignmentId id,GameplayContext context={});`
- `ae570838e9e4de2f` | `QUERY` | `epidemic::gameplay::roles_jobs::JobAssignmentId` | `[[nodiscard]] constexpr auto operator<=>(const JobAssignmentId&)const noexcept=default;`
- `b0ee08fb2b1f3c1b` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> CancelDuty(DutyId id,GameplayContext context={});`
- `ba872d7a64433b96` | `LIFECYCLE` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] std::vector<DutyId> ActivateDueShifts(GameplayTimePoint now,GameplayContext context={});`
- `bc37be652c8f9045` | `FACTORY` | `epidemic::gameplay::roles_jobs::JobAssignmentId` | `static constexpr JobAssignmentId FromString(std::string_view s)noexcept`
- `bea666033dc2e007` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] const JobAssignment*GetJobAssignment(JobAssignmentId id)const noexcept;`
- `bfb8cfb659e66109` | `LIFECYCLE` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] std::vector<DutyId> ActivateDueShifts(GameplayTimePoint from,GameplayTimePoint to,GameplayContext context={});`
- `bfdcbb10175b1959` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> CompleteDuty(DutyId id,GameplayContext context={});`
- `bfe815df66e57351` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] std::vector<Duty> FindActiveDuties()const;`
- `c172efae89404cb4` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `void PruneChangesBefore(std::uint64_t sequence)noexcept;`
- `c975c35cef2cf6a9` | `FACTORY` | `epidemic::gameplay::roles_jobs::JobTaskTypeId` | `static constexpr JobTaskTypeId FromString(std::string_view s)noexcept`
- `cd28827d95832c73` | `QUERY` | `epidemic::gameplay::roles_jobs::JobAssignmentId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d1efae2eca9fddcd` | `FACTORY` | `epidemic::gameplay::roles_jobs::WorkScheduleId` | `static constexpr WorkScheduleId FromString(std::string_view s)noexcept`
- `d23a14a4960a53c7` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkScheduleId` | `[[nodiscard]] constexpr auto operator<=>(const WorkScheduleId&)const noexcept=default;`
- `d293738ca91792f3` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(RolesJobsSnapshot snapshot);`
- `d7dc16153e22ebdb` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkShiftId` | `[[nodiscard]] constexpr auto operator<=>(const WorkShiftId&)const noexcept=default;`
- `d8ff3b91067eda71` | `FACTORY` | `epidemic::gameplay::roles_jobs::WorkScheduleId` | `static constexpr WorkScheduleId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `e04fcf75991d9730` | `QUERY` | `epidemic::gameplay::roles_jobs::JobTaskTypeId` | `[[nodiscard]] constexpr bool operator==(const JobTaskTypeId&)const noexcept=default;`
- `e755e5a0583a306b` | `QUERY` | `epidemic::gameplay::roles_jobs::JobAssignmentId` | `[[nodiscard]] constexpr bool operator==(const JobAssignmentId&)const noexcept=default;`
- `ea67074a43185c73` | `FACTORY` | `epidemic::gameplay::roles_jobs::DutyId` | `static constexpr DutyId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `ee346fa834f4efa7` | `MUTATOR` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] foundation::Result<void> CancelAssignment(JobAssignmentId id,GameplayContext context={});`
- `f2f97f8b128503e8` | `QUERY` | `epidemic::gameplay::roles_jobs::DutyId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `f39ba4abe566ece5` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkScheduleId` | `[[nodiscard]] constexpr bool operator==(const WorkScheduleId&)const noexcept=default;`
- `f5f485fd1d78b51b` | `QUERY` | `epidemic::gameplay::roles_jobs::RolesJobsService` | `[[nodiscard]] std::vector<Workplace> FindWorkplacesInArea(GameplayObjectRef area)const;`
- `fa97821ed328450d` | `QUERY` | `epidemic::gameplay::roles_jobs::WorkShiftId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
