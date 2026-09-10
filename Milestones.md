Это 10 клучевых точек, которые нужно пройти до стабилизации движка.

1. Архитектурная целостность и freeze границ

Статус: **ВЫПОЛНЕНО (2026-09-09)**.

Цель: окончательно зафиксировать структуру слоёв, направленность зависимостей, ownership и публичные границы модулей.

Шаг считается выполненным, когда:
EngineBase ничего не знает о Runtime и Framework.
EngineRuntime ничего не знает о Framework.
Framework работает с Runtime только через утверждённые boundary-механизмы.
Внутри Framework gameplay majors не образуют запрещённые горизонтальные зависимости.
Все architecture validators и CMake проверки проходят.
Public API больше не содержит временных обходных контрактов, которые заведомо придётся ломать на следующем этапе.

После этого архитектурная форма движка считается замороженной.

2. Correctness всех локальных модулей

Статус: **ВЫПОЛНЕНО** (2026-09-09). Base и Runtime проходят независимые, интеграционные и regression test suites; failure-atomicity, lifecycle, invalid input, stale identity, overflow и exception paths закреплены тестами. Итоговая Debug-сборка и полный набор тестов: **88/88**.

Цель: каждый major должен быть корректен сам по себе без интеграции с соседями.

Проверяются:
lifecycle объектов;
create/update/remove;
ошибочные входные данные;
duplicate IDs;
stale handles;
revision;
generation;
freeze registries;
invalid state transitions;
overflow;
underflow;
empty state;
boundary values;
exception paths.

Шаг завершён, когда каждый major проходит собственный независимый test suite, а для каждого публичного mutation API определены и протестированы preconditions, postconditions и failure semantics.

Главный критерий: ошибка caller не может оставить сервис в неконсистентном состоянии.

3. Persistence, snapshot и restore hardening

Цель: любое сохраняемое состояние должно безопасно переживать save/load.

Для каждого state owner проверяются:
snapshot полнота;
валидация snapshot;
restore в temporary state;
atomic replacement;
ID generator state;
revision;
generation;
journal cursor;
persistent/session разделение;
corrupted input;
старые версии;
частично отсутствующие optional данные.

Шаг выполнен, когда выполняется инвариант:

```text
State A
-> Snapshot
-> Destroy service
-> Restore
-> State A'
```

и `A'` семантически эквивалентно `A`.

Повреждённый snapshot никогда не должен частично менять рабочее состояние.

Дополнительно нужен whole-engine restore, где восстанавливаются все три слоя и проверяется правильный порядок зависимых систем. Дополнительно нужно написать сценарии тестов, которые намеренно ломают логику восстановления и проверить устойчивость системы.

4. Determinism и replayability

Цель: одинаковый initial state плюс одинаковые inputs должны давать одинаковый результат.

Проверяются:
unordered containers;
iteration order;
random streams;
event order;
task completion order;
floating point sensitive paths;
wall clock leakage;
unstable IDs;
pointer based ordering;
serialization ordering.

Нужно иметь deterministic scenario runner:

```text
seed
initial snapshot
input stream
N ticks
```

который несколько раз выдаёт одинаковый final-state hash.

Шаг завершён, когда deterministic сценарии стабильно совпадают между повторными запусками и после save/load в середине сценария.

5. Memory и lifetime hardening

Цель: движок должен быть пригоден для многочасовой seamless session.

Нужно пройти:
journals;
event histories;
tombstones;
pending operations;
reconciliation queues;
resource caches;
runtime bindings;
streaming records;
deleted identities;
terminal records;
subscriptions;
callbacks;
temporary buffers.

Проверяется не только heap leak, но и logical retention.

Шаг завершён, когда длительный stress run показывает bounded memory behaviour. После выхода системы на steady state память не должна линейно расти от числа уже завершённых событий.

Дополнительно обязательны sanitizer или эквивалентные проверки на use-after-free, double free, invalid access и lifetime bugs.

6. Concurrency и async safety

Цель: определить, что именно разрешено выполнять параллельно, и доказать безопасность этих сценариев.

Для каждого сервиса должно быть однозначно известно:
single-thread only;
multiple readers;
single writer;
fully concurrent;
external synchronization required.

Проверяются:
TaskScheduler;
resource loading;
streaming;
queries;
event dispatch;
renderer preparation;
physics interaction;
save operations;
simulation;
callbacks.

Нужны stress tests с конкурентными readers/writers там, где API это разрешает.

Шаг выполнен, когда нет data race, deadlock, lost update и use-after-free при официально поддерживаемых concurrency сценариях.

7. Integration correctness между majors и слоями

Цель: проверить уже не отдельные сервисы, а причинные цепочки.

Например:

```text
Interaction
-> Effects
-> Conditions
-> Perception
-> Knowledge
-> AI
```

или:

```text
Process
-> ResourceProduction
-> Economy
-> Population
-> NeedsLife
```

или:

```text
Framework semantic object
-> RuntimeBridge
-> Runtime materialization
-> Physics
-> observation
-> Framework
```

Для каждого integration target нужны independent tests.

Особое внимание:
двойное применение событий;
потеря событий;
stale generation;
stale revision;
циклическая реакция integration;
неправильный порядок обработки;
частичное выполнение;
resync после restore.

Шаг выполнен, когда integration может быть удалена без нарушения самостоятельной корректности majors, а при её наличии полностью соблюдается single-owner state model.

8. Load, scale и degradation tests

Цель: проверить, что архитектура выдерживает реальные масштабы будущей игры.

Нужно задавать нагрузку порядками величины, а не десятками объектов.

Например:
100 000+ semantic entities;
миллионы historical operations за длительный прогон;
десятки тысяч inactive NPC;
массовый streaming churn;
тысячи scheduled events;
крупные inventories;
большие knowledge graphs;
массовые economy/process updates;
очереди RuntimeBridge;
destruction/materialization cycles.

Проверяются:
CPU;
memory;
allocations;
latency;
queue depth;
frame spikes;
complexity degradation.

Шаг считается завершённым не тогда, когда всё работает быстро, а когда известны и проверены границы нагрузки, а overload приводит к контролируемой деградации, budget exceeded, coalescing, backpressure или deferred work вместо падения или бесконечного роста очереди.

9. Full regression suite и failure injection

Цель: после каждого исправления больше не возвращаться к тем же классам ошибок.

Каждый найденный баг должен превращаться в regression test.

Дополнительно нужны deliberate failure scenarios:
allocation failure там, где практически возможно проверить;
invalid serialization;
backend failure;
provider exception;
callback exception;
queue overflow;
missing resource;
stale handle;
destroy during pending operation;
runtime object disappears;
save во время сложного state;
load после partial runtime teardown.

К этому этапу CI должен запускать:
unit;
integration;
persistence;
determinism;
stress subset;
regression;
sanitizer configurations.

Milestone завершён, когда новая правка не может попасть в основную ветку при нарушении уже проверенного инварианта.

10. Whole-engine smoke qualification и freeze

Это финальный gate перед продолжением разработки Tools, Reflection, Scripting и самой игры.

Нужно несколько законченных сценариев, которые используют сразу значительную часть движка.

Например один сценарий:

```text
boot
load world
create population
materialize area
spawn entities
interactions
combat
inventory changes
NPC perception
knowledge
AI reaction
economy/process simulation
stream away
coarse simulation
save
shutdown
boot
load
stream area back
continue
```

Второй сценарий должен специально делать длительный simulation run.

Третий должен постоянно materialize/dematerialize регионы.

Четвёртый может быть destruction heavy.

Пятый должен проверять save/load несколько раз внутри одного causal сценария.

Финальный критерий freeze:

```text
architecture checks = PASS
unit tests = PASS
integration tests = PASS
persistence tests = PASS
determinism tests = PASS
regression tests = PASS
sanitizers = PASS
stress qualification = PASS
whole-engine smoke scenarios = PASS
known P0/P1 defects = 0
```