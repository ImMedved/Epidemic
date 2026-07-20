# Контракты EngineRuntime

## Основные правила

EngineRuntime состоит из majors, а не из одного monolith. Каждый major владеет только своим runtime state и предоставляет public contracts для наблюдения, команд и composition.

- Concrete gameplay rules, content behavior и final product composition не находятся в EngineRuntime.
- RuntimeFoundation хранит общий словарь: ids, time, budgets, spatial math и shared state helpers.
- Cross-major mutation напрямую запрещена.
- Data crossing выполняется через ids, immutable snapshots, projections, events, command queues, proposal queues или Support adapters.
- Support является composition layer. Он знает majors; majors не знают Support.

## Error model

Expected failures возвращаются через `foundation::Result<T>` и stable error codes в форме `major.reason`.

Human-readable messages могут меняться. Tests и callers должны ветвиться по error code, а не по тексту diagnostics.

Новые public failures добавляют новый code. Нельзя переиспользовать существующий code с новым смыслом без migration notes.

## Determinism

Public queries, manifests, histories и work lists, которые описаны как deterministic, сортируются по stable id, sequence или explicit priority. Internal `unordered_map` order не должен становиться observable behavior.

Budgets ограничивают новую работу или минимальные progressive units согласно contract конкретного major. Если budget является soft из-за неизвестного размера backend work, это фиксируется в module doc и tests.

## Handles и revisions

Lifetime-sensitive public operations используют generation handles или явно documented never-reuse ids. Raw ids могут присутствовать в snapshots и lookup records, но не должны обходить lifetime validation.

Externally visible authoritative state несет revision. Revision меняется при observable state mutation. Transient bookkeeping не должен выглядеть как authoritative revision change, если module doc не говорит иначе.

## Backend boundaries

Backend implementation получает neutral descriptors, handles и payload interfaces. Runtime handle не должен протекать в backend contract, если backend не может честно владеть таким id. Runtime преобразует backend handles в public runtime handles через свой registry.

Reference implementations нужны для tests и базовой composition. Они не подменяют production backend неявно.

# Зависимости EngineRuntime

## Direction

Базовое направление зависимостей:

```text
EngineBase
    <- RuntimeFoundation
        <- Runtime majors
            <- Support
                <- GameFramework
                    <- Game
```

`Support` может зависеть от всех runtime majors, потому что он их компонует. Ни один runtime major не должен зависеть от `Support`.

## Major boundary checklist

- RuntimeFoundation не зависит от других runtime majors.
- Assets не зависит от Resources и не управляет payload lifetime.
- Resources не зависит от World, Renderer, Persistence или Simulation.
- Serialization не зависит от Persistence.
- Persistence не владеет World implementation.
- Time не зависит от Environment, World или Simulation.
- Environment не командует Renderer, Physics, Audio, Navigation или Simulation.
- Scene не знает World, Renderer или Physics storage.
- World не владеет Scene, Persistence, Renderer или Physics objects.
- Streaming не владеет World, Resources или Persistence state.
- Renderer не управляет resource loading policy.
- Physics не пишет Scene или World напрямую.
- Navigation не принимает AI decisions.
- Animation не содержит movement/action rules.
- Audio не содержит content-direction logic.
- Simulation не владеет authoritative domain state.

## Allowed composition adapters

Cross-major adapters живут в Support или верхнем composition layer, используют только public contracts и не содержат gameplay logic.

Текущая ожидаемая карта adapters:

- Scene -> Renderer
- Resources -> Renderer
- Scene -> Physics
- Resources -> Animation
- Animation -> Renderer
- Resources -> Audio
- Scene -> Audio
- Environment -> Audio
- Environment -> Navigation
- World/Resources/Persistence -> Streaming
- Streaming -> World
- Time -> Simulation
- Simulation -> domain commit target

Пока `Support` не frozen, этот список фиксирует архитектурное направление, но не объявляет завершенный Support API.

## CMake boundary

Runtime majors должны оставаться standalone targets. `Support` является единственным runtime target, которому разрешено link-ить полный набор majors. Architecture tests должны ловить попытки добавить `EpidemicRuntimeSupport` как dependency обычного major.

Private `src` headers не входят в public dependency graph и не должны использоваться cross-major.

# Lifecycle, update и shutdown

## Frame order

Baseline frame order принадлежит Support/composition code, а не прямым вызовам между majors.

1. Advance Time.
2. Apply completed main-thread commits.
3. Process Resources.
4. Process Streaming budgets.
5. Tick Simulation jobs and memory expiration budgets.
6. Tick Navigation path and tile budgets.
7. Tick Animation playback and pose snapshots.
8. Step Physics fixed ticks.
9. Commit Scene-facing projections.
10. Update Audio emitters, listeners and events.
11. Prepare and submit Renderer.
12. Publish diagnostics and event buffers.

Этот порядок не дает major права включать private implementation другого major. Данные проходят через public contracts, immutable snapshots, event buffers и adapters.

`RuntimeFrameInput::real_delta` является исходной frame duration. Time возвращает authoritative game delta через `TimeAdvanceResult`; upper code не должен рассчитывать вторую независимую game delta.

## Main-thread commits

Background work может готовить detached proposals из immutable input. Authoritative mutation возвращается через runtime thread, explicit commit phase или module-owned synchronization point.

Simulation proposals commit-ятся через `ISimulationCommitTarget`. Invalid или stale proposal не должен менять authoritative domain state частично.

## Shutdown order

Shutdown начинается с запрета новой работы. Default order:

1. Stop accepting new work.
2. Cancel or drain simulation, navigation and streaming jobs.
3. Commit or explicitly discard valid pending proposals.
4. Stop Audio and destroy backend voices/listeners.
5. Stop Physics and destroy backend bodies/shapes.
6. Release Animation and Renderer resources.
7. Release Renderer leases.
8. Unload Streaming state.
9. Finish or rollback Persistence transactions.
10. Flush Persistence according to durability policy.
11. Destroy Support-owned adapters.
12. Destroy services.

Shutdown должен быть idempotent после полного успеха. Если cleanup failure оставляет external ownership, повторный shutdown должен иметь достаточно сохраненной информации, чтобы retry-ить операцию.

Persistence durability failure не считается successful durable shutdown. Failed cleanup не должен маскироваться как success.


# Владение и lifetime

## Services

Factories возвращают service bundles. Bundle содержит interfaces и owned implementation dependencies, необходимые конкретному major. Код должен хранить только те interfaces, которые реально нужны.

Composition layer владеет long-lived adapters и external backends. Raw pointers в options являются non-owning adapter ports, если contract явно не говорит обратное.

## Snapshots и payloads

Query APIs возвращают values или immutable published data. Snapshots, видимые снаружи, несут revision, если state может устаревать.

Resource payloads и renderer/animation/audio-facing ресурсы передаются как immutable data или typed resource interfaces. Consumer не должен менять payload после publication.

## Handles

Handle является identity token, а не владением, если contract прямо не называет его lease.

`ResourceLease` является ownership token и освобождается ровно один раз. `ResourceHandle` остается query identity и не освобождает чужое удержание.

Streaming demand, animation animator, physics body, audio emitter/listener, navigation query и simulation job handles carry generation. Stale handle возвращает stable error, а не silently no-op.

## Borrowed dependencies

Projection/source interfaces используются как borrowed dependencies или `shared_ptr`-owned dependencies согласно factory options. Borrowed dependency должен жить дольше runtime, который его использует.

Support-owned adapters могут удерживать resource leases или backend handles, если это часть documented bridge. Shutdown обязан освобождать такие ownership records retryable способом: failed cleanup не должен терять handle, lease или voice/body/request id.

## Event buffers

Event buffers принадлежат major. Consumers читают события в frame orchestration и очищают buffer только в разрешенной phase. Partial publication после failed backend batch запрещена, если module contract не объявляет диагностическое отбрасывание invalid event.

## Raw ownership

Undocumented raw ownership запрещен. Любой `T*` в public options считается non-owning, пока contract явно не говорит обратное.

# Threading policy

## Baseline

EngineRuntime reference implementations используют простой baseline:

- mutation выполняется на runtime/main thread или в explicitly controlled commit phase;
- background jobs работают только с detached immutable input;
- commit work возвращается через main-thread phase или module-owned synchronization point;
- snapshots являются value objects или immutable published data;
- registries не являются thread-safe, если public documentation явно не говорит обратное.

Этот contract намеренно не добавляет mutex в каждый класс. Synchronization принадлежит orchestration, commit phases или concrete backend implementation.

## Public notes

Mutable public registries и managers должны сохранять короткое правило:

```cpp
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
```

Если future backend использует worker threads, он не получает права менять authoritative state напрямую. Он публикует result через public queue, sink, event buffer или commit target.

## Stale work

Generation handles и revisioned snapshots являются основным способом обнаружить stale background work на commit boundary. Commit code проверяет expected revision/generation и возвращает stable error вместо silent overwrite.
