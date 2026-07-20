# Simulation

## Назначение

Simulation предоставляет budgeted выполнение независимых jobs, attention/relevance, world memory, abstract facts, scheduled tasks и очередь proposals. Он не содержит конкретную жизнь NPC, экономику или gameplay rules.

## Jobs

Caller передает `shared_ptr<ISimulationJob>` и metadata. Scheduler выполняет `ExecuteStep()` под `RuntimeBudget`, контролирует generation handle и state transitions. Job может завершиться, ожидать main-thread publication или продолжить работу. Scheduled task активирует существующий job в `SimulationTime`.

`CancelJob()` удаляет связанное schedule. Shutdown запрещает новую работу и retry-ит failed cancellation.

## Proposals

Job не меняет authoritative domain напрямую. `SimulationProposalBatch` публикуется только от `Completed` или `WaitingForMainThread` job. Queue commit-ит через `ISimulationCommitTarget`; terminal job удерживается до исчезновения последнего pending batch. `CommitNext()` и `DiscardAll(reason)` сразу запускают pruning.

## Memory и facts

World memory event имеет неотрицательное `happened_at`; TTL меньше нуля запрещен, ноль означает отсутствие автоматического expiration. Capacity является жестким пределом и не вытесняет active events. Abstract facts валидируют domain, kind, type, zone, subject и time. Attention finite и находится в `[0,1]`.

## Стабильность

После финальных invariants модуль frozen. Конкретные NPC и economy jobs реализуются в GameFramework.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `simulation.h`: factory-функции или backend implementation без отдельного публичного типа.
- `simulation_runtime.h`: `ISimulationJob`, `ISimulationScheduler`, `ISimulationRuntime`, `IAttentionSystem`, `IWorldMemory`, `IRelevancePolicy`, `ISimulationCommitTarget`, `IAbstractFactStore`, `ISimulationProposalQueue`, `IScheduledSimulationTasks`, `ISimulationClock`, `SimulationDependencies`, `SimulationServices`.
- `simulation_types.h`: `SimulationJobId`, `WorldMemoryEventId`, `ScheduledSimulationTaskId`, `SimulationJobHandle`, `SimulationLane`, `SimulationZoneState`, `SimulationJobState`, `MemoryLifetime`, `ObservationState`, `AttentionScore`, `SimulationBudget`, `SimulationJobDesc`, `SimulationStepInput`, `SimulationProposal`, `SimulationProposalBatch`, `SimulationStepResult`, `SimulationTickFailure`, `SimulationTickResult`, `WorldMemoryEvent`, `AbstractFact`, `ScheduledSimulationTask`, `ScheduledTaskFailure`, `ScheduledTaskExecutionResult`, `ProposalDiscardReason`, `WorldMemoryQuery`, `SimulationOptions`.
