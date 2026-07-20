# Assets

## Назначение

Assets хранит стабильное описание данных независимо от того, загружены ли они в память. Он связывает `AssetId`, `AssetType`, metadata, dependencies и location, но не читает payload и не управляет memory residency.

## Контракты

`IAssetCatalog` выполняет queries, `IAssetCatalogWriter` регистрирует и изменяет metadata, а `IAssetLocationResolver` превращает logical location в доступный путь или package entry. `AssetServices` объединяет эти interfaces.

`AssetMetadata` содержит identity, type, state, location, tags и dependencies. `AssetDependencyManifest` строится детерминированно, удаляет diamond duplicates и сохраняет optional missing dependencies для diagnostics.

## Инварианты

Absolute paths и traversal за пределы mount отклоняются. Package и virtual locations требуют valid mount ID. Required missing dependency делает manifest invalid; optional dependency может отсутствовать. Query results сортируются по ID.

## Граница

Assets не зависит от Resources и не возвращает loaded object. Resource loader использует catalog как источник metadata. Модуль frozen.

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
