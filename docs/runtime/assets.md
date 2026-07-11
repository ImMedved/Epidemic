# Assets

## Purpose

Provides asset metadata contracts and in-memory catalog behavior without taking ownership of resource loading.

## Includes

- asset metadata and type descriptors
- read-only `IAssetCatalog`
- write-side `IAssetCatalogWriter`
- location resolver and dependency manifest contracts

## Excludes

- resource handles
- loader execution
- world or gameplay logic

## Public Contracts

- `IAssetCatalog`
- `IAssetCatalogWriter`
- `IAssetLocationResolver`
- `AssetMetadata`, `AssetLocation`, `AssetType`

## Forbidden Dependencies

No dependency on `Resources`.

## States

Asset state is metadata-only and does not manage runtime residency.

## How To Use

Register asset metadata through the writer interface and query through the read-only catalog interface.

## Example

```cpp
catalog_writer.RegisterAsset(metadata);
auto by_type = catalog.FindByType(type_id);
```

## Testing Strategy

Validate registration, read queries, tag/type lookup and separation of read/write interfaces.
