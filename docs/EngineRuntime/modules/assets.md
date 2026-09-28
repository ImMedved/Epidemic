# Assets

## Назначение

Assets хранит immutable после регистрации metadata catalog независимо от того, загружен ли payload в память. Модуль связывает `AssetId`, `AssetType`, `AssetState`, logical `AssetLocation`, tags и dependency metadata. Он не читает payload, не владеет resource residency и не выполняет filesystem/package I/O.

## Контракты

`IAssetCatalog` предоставляет detached read/query API. `IAssetCatalogWriter` регистрирует новые записи и необратимо переводит catalog в sealed state. Update/replace API отсутствует. `IAssetLocationResolver` возвращает сохранённый logical `AssetLocation`; он не преобразует его в host path и не монтирует package. `AssetServices` предоставляет эти interfaces над одним in-memory catalog.

`AssetMetadata::version` является версией metadata/content конкретного asset. Это не global catalog revision. Значение `0` для зарегистрированной metadata недопустимо.

## Registration и location validation

Регистрация требует valid `AssetId`, `AssetType`, `AssetState` и `AssetLocationKind`. Path обязан быть непустым engine-root-relative значением без embedded NUL, host absolute/rooted form, UNC/device form, Windows drive-relative form и traversal за engine root. `PackageEntry` и `VirtualPath` требуют valid `mount_id`. `Generated` требует valid `generator_id`.

Raw path, dependency list и tag list ограничены `kMaxAssetPathBytes`, `kMaxAssetDependencies` и `kMaxAssetTags`. Limits проверяются до каталожной mutation. Dependency IDs и tags должны быть valid, self dependency запрещена, duplicate dependency и duplicate tag отклоняются. После validation path lexical-normalized. Allocation failure во время validation, normalization или insertion не публикует partial catalog entry.

## Dependency manifest

`BuildDependencyManifest` обходит dependency graph итеративно. Required missing dependency завершает query controlled failure. Optional missing dependency остаётся в manifest. Diamond dependencies дедуплицируются по `AssetId`. Если один asset достижим и по optional, и по required edge, итоговый `required` является logical OR всех достижимых incoming edges и поэтому не зависит от registration или traversal order. Cycle возвращает controlled failure. Итоговый manifest сортируется по `AssetId::Raw()`.

## Queries и lifecycle

`FindById`, `FindByType`, `FindByTag` и `Resolve` возвращают detached copies. Type/tag queries и dependency manifest имеют deterministic ordering. Invalid selector для non-Result query наблюдается как empty/not-contained. Result-producing `Resolve` и `BuildDependencyManifest` отличают invalid ID от valid-but-missing ID.

`Seal()` идемпотентен. После первого successful seal catalog больше не принимает registration и не возвращается в mutable state. Модуль не имеет shutdown/cleanup lifecycle, внешних callbacks/backends, generated revisions, journals или derived indexes.

Mutation выполняется на runtime thread. Concurrent read/write не поддерживается. После завершённого bootstrap и `Seal()` состояние больше не мутирует; const queries не имеют hidden mutation.

## Persistence boundary

Assets не публикует snapshot/restore API. Goal 3.5 не вводит скрытый catalog persistence contract. Если persistent asset catalog потребуется composition/bootstrap layer, он должен производить обычные validated registrations через существующий writer contract.

## Статус Goal 3

После локального аудита Goal 3.5, повторной MSVC Debug/Release qualification и canonical evidence convergence модуль имеет статус `LOCAL_READY`. Это не означает `SYSTEM_READY` или `FROZEN`; системные и финальные freeze gates остаются в последующих целях общего плана.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `asset_catalog.h`: `IAssetCatalog`.
- `asset_catalog_writer.h`: `IAssetCatalogWriter`.
- `asset_dependency_manifest.h`: `AssetDependencyManifest`.
- `asset_location.h`: `AssetLocationKind`, `AssetLocation`.
- `asset_location_resolver.h`: `IAssetLocationResolver`.
- `asset_metadata.h`: `AssetDependency`, `AssetMetadata`.
- `asset_services.h`: `AssetsOptions`, `AssetServices`.
- `asset_state.h`: `AssetState`.
- `asset_type.h`: `AssetType`.
