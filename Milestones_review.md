# Проверка milestones

## 1. Архитектурная целостность и freeze границ

Статус: **ВЫПОЛНЕНО** (2026-09-09).

### Зафиксированные границы

- EngineBase не зависит от EngineRuntime или EngineFramework.
- EngineRuntime не зависит от EngineFramework.
- Runtime majors могут зависеть от RuntimeFoundation; межмодульная композиция принадлежит Runtime Support.
- Framework base infrastructure и gameplay state owners не зависят напрямую от Runtime, Platform, RHI или D3D11.
- RuntimeBridge является утверждённой границей для Runtime World, Physics, Environment и Navigation.
- CoreIntegration/RuntimeTimeAdapter является единственным утверждённым прямым адаптером IntegrationLayer к RuntimeTime.
- Горизонтальные зависимости gameplay majors контролируются существующим Framework validator; композиция majors принадлежит IntegrationLayer.

Правила документированы в `docs/architecture.md` и автоматически проверяются `cmake/ArchitectureFreeze.cmake` по `LINK_LIBRARIES`, `INTERFACE_LINK_LIBRARIES` и production includes.

### Public API freeze

- Incremental journal API использует только `ChangeCursor { epoch, sequence }`.
- Sequence-only `ChangesSince`, `ReadChangesSince`, `LatestChangeSequence` и adapter `TypedCursor` обходы удалены из public API.
- Restore меняет epoch; cursor предыдущей epoch всегда требует `snapshot_required`, независимо от совпадения sequence.
- Удалён legacy timed-production executor из ResourcesProduction; выполнение производственных процессов принадлежит Processes.
- Удалены временные compatibility-поля NarrativeSnapshot и PopulationSnapshot.
- `ObservationKnowledgeResult::knowledge` обязателен при success.
- `KnowledgeAIAdapter::BuildContext` требует явный `AIExecutionAvailability`.
- Target `EpidemicPublicHeaderSelfContainment` компилирует каждый публичный header отдельной translation unit.

### Автоматизация

- `CMakePresets.json` содержит профили `base-debug`, `runtime-debug` и `full-debug`.
- `.github/workflows/architecture-freeze.yml` запускает ту же матрицу при push и pull request.
- Architecture freeze checks включены по умолчанию через `EPIDEMIC_ARCHITECTURE_FREEZE_CHECKS=ON`.

### Проверки

- Base-only configure/build: успешно.
- Base-only tests: **9/9**.
- Runtime-without-Framework configure/build: успешно.
- Runtime-without-Framework tests: **29/29**, включая `EpidemicRuntimeArchitectureTests`.
- Full configure/build: успешно.
- Все публичные headers отдельно: успешно.
- Full tests: **88/88**.
- `git diff --check`: ошибок форматирования нет.

### Итог

Критерии пункта 1 выполнены. Изменение архитектурного allowlist теперь требует явного изменения validator, документации и CI-матрицы. Пункт 1 считается замороженным.

## 2. Correctness всех локальных модулей

Статус: **ВЫПОЛНЕНО** (2026-09-09).

- Base: закрыты scheduler self-wait/self-destruction, shutdown ordering, invalid frame phase, bounded EventBus drain, Win32 owner-thread lifecycle, D3D11 failed-resize recovery и noexcept profiling cleanup.
- Runtime: публичные mutation paths получили preflight validation, checked revision/identity arithmetic, enum-domain validation, staged publication, exception boundaries и retryable cleanup для внешних ресурсов.
- Boot-time registries имеют freeze contracts; намеренно динамический Physics shape registry явно документирован и тестирует register/unregister lifecycle.
- Renderer переходит в терминальный `ShuttingDown` при первом shutdown и запрещает новую работу, сохраняя повторный cleanup.
- Audio сохраняет ownership backend listeners при неудачном rollback и освобождает их при повторном shutdown.
- Serialization публикует десериализованный candidate только через `noexcept` swap; неподдерживающие strong guarantee типы отклоняются до mutation.

### Проверки

- Base test suites: **9/9**.
- Runtime test suites: **20/20**.
- Полная Debug-сборка: успешно.
- Полный набор Base + Runtime + Framework: **88/88**.
- `git diff --check`: ошибок форматирования нет.

Критерий failure semantics выполнен: проверенные caller, allocation, backend и extension failures не оставляют authoritative state без ownership либо в неописанном lifecycle state. Пункт 2 считается закрытым.
