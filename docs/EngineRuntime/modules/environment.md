# Environment

## Назначение

Environment хранит нейтральное состояние погоды, сезона, климата и поверхностей по регионам и предоставляет snapshots/projections другим systems.

## Модель

Регион регистрируется атомарно с `WeatherState`, `SeasonState` и `ClimateProfile`. `IEnvironmentWriter` изменяет состояния, `IEnvironmentQuery` читает их, `IEnvironmentUpdatePolicy` проверяет revisioned updates, а `IEnvironmentRuntime` применяет обновления.

Climate averages описывают долгосрочный регион, а current temperature/humidity принадлежат Weather. Surface state имеет immutable region ownership.

## Revisions

Есть global revision и per-region revision. Cross-region update не создает ложный conflict. No-op setter не увеличивает revision. Перемещение существующего `SurfaceId` в другой region не выполняется обычным `SetSurfaceState`.

## Граница

Environment не решает, как рисовать дождь, рассчитывать навигацию или управлять поведением NPC. Он предоставляет состояние и projection. Модуль frozen.

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
