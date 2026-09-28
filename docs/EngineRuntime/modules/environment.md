# Environment

## Назначение

Environment хранит нейтральное состояние погоды, сезона, климата и поверхностей по регионам и предоставляет snapshots/projections другим systems.

## Модель

Регион регистрируется атомарно с `WeatherState`, `SeasonState` и `ClimateProfile`. `IEnvironmentWriter` изменяет состояния, `IEnvironmentQuery` читает их, `IEnvironmentUpdatePolicy` проверяет revisioned updates, а `IEnvironmentRuntime` применяет обновления.

Climate averages описывают долгосрочный регион, а current temperature/humidity принадлежат Weather. Surface state имеет immutable region ownership.

## Revisions

Есть global revision и per-region revision. Cross-region update не создает ложный conflict. No-op setter не увеличивает revision. Перемещение существующего `SurfaceId` в другой region не выполняется обычным `SetSurfaceState`.

## Registration и ownership

`RegisterRegionEnvironment` публикует регион только после полной проверки weather/season/climate и reservation следующей revision. `FreezeRegistration()` идемпотентен и после freeze запрещает новые `RegionId` и новые `SurfaceId`; mutation уже зарегистрированных region/surface state остаётся разрешена. `SurfaceId` имеет immutable region ownership и не переносится обычным update.

`SurfaceState::condition` является derived output: входное значение не является authoritative и перед публикацией всегда вычисляется через `DeriveSurfaceCondition` из wetness/snow/mud/ice. Поэтому enum-domain guarantee для сохранённого surface state обеспечивается derivation, а не доверием caller-provided condition.

## Validation и transaction semantics

Weather/season enums валидируются. Нормированные величины ограничены контрактными диапазонами, остальные числовые значения обязаны быть finite; wind speed и surface depths неотрицательны. Wind direction принимает любое finite значение и канонизируется modulo 360 перед сравнением и commit.

`ApplyUpdate` использует per-region `source_revision`. Весь batch, включая ownership и duplicate `SurfaceId`, валидируется до commit. Invalid entry, revision conflict, revision exhaustion и allocation failure сохраняют authoritative region/surface state без partial publication. Semantic no-op не увеличивает global или per-region revision.

`IEnvironmentUpdatePolicy` является внешней callback boundary. Result failure передаётся вызывающему коду как controlled failure, exception преобразуется в `environment.update_policy_exception`, а returned update проходит обычную полную `ApplyUpdate` validation до mutation.

## Snapshot и projection

`EnvironmentSnapshot` является detached runtime observation одного региона. Surface collection сортируется по `SurfaceId`, поэтому observable order не зависит от `unordered_map` iteration. Snapshot содержит region revision, weather, season, climate, derived temperature/humidity и все surfaces региона. `EnvironmentProjection` строится из того же authoritative observation и несёт ту же region revision.

Локального restore API у Environment нет. В Goal 3 `EnvironmentSnapshot` не объявляется persistence format; persistence mapping решается отдельно в Goal 5.

## Граница

Environment не решает, как рисовать дождь, рассчитывать навигацию или управлять поведением NPC. Он предоставляет состояние и projection. После Goal 3 и canonical evidence convergence модуль имеет статус `LOCAL_READY`; до системных Goals 5–9 он не должен называться `FROZEN`.

## Goal 3.14 audit defect

`G3-ENV-AUDIT-001`: `FreezeRegistration()` блокировал регистрацию нового региона, но не блокировал первое появление нового `SurfaceId` через `SetSurfaceState()` или `ApplyUpdate()`. Это нарушало registration contract Goal 3.14: после freeze новые region/surface identities не публикуются, при этом runtime updates уже зарегистрированных identities остаются разрешены. Причина: freeze guard находился только в `RegisterRegionEnvironment()`. Исправление: reject нового surface до revision/allocation/mutation как в direct, так и batch path; существующий surface по-прежнему обновляется. Regression: direct и batch creation после freeze возвращают `environment.registration_frozen`, global/per-region revisions и maps не меняются, existing surface update проходит.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `climate_profile.h`: `ClimateProfile`.
- `environment_projection.h`: `EnvironmentProjection`.
- `environment_runtime.h`: `IEnvironmentQuery`, `IEnvironmentWriter`, `IEnvironmentUpdatePolicy`, `IEnvironmentRuntime`.
- `environment_services.h`: `EnvironmentOptions`, `EnvironmentServices`.
- `environment_snapshot.h`: `EnvironmentSnapshot`.
- `environment_update.h`: `EnvironmentUpdateInput`, `EnvironmentStateUpdate`.
- `season_state.h`: `SeasonKind`, `SeasonState`.
- `surface_state.h`: `SurfaceConditionKind`, `SurfaceState`.
- `weather_state.h`: `WeatherKind`, `WeatherState`.
