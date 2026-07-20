#include "in_memory_asset_catalog.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] std::string NormalizePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    const bool absolute = path.starts_with('/');

    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '/'))
    {
        if (part.empty() || part == ".")
        {
            continue;
        }

        if (part == "..")
        {
            if (!parts.empty() && parts.back() != "..")
            {
                parts.pop_back();
                continue;
            }
            parts.push_back(part);
            continue;
        }

        parts.push_back(part);
    }

    std::string normalized;
    for (const auto& element : parts)
    {
        if (!normalized.empty())
        {
            normalized += '/';
        }
        normalized += element;
    }
    return absolute ? "/" + normalized : normalized;
}

[[nodiscard]] bool IsAbsolutePath(std::string_view path) noexcept
{
    return path.starts_with('/') || (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
                                    path[1] == ':' && path[2] == '/');
}

[[nodiscard]] bool IsSafeRelativePath(std::string_view path) noexcept
{
    if (path.empty() || IsAbsolutePath(path))
    {
        return false;
    }

    std::stringstream stream{std::string{path}};
    std::string part;
    int depth = 0;
    while (std::getline(stream, part, '/'))
    {
        if (part.empty() || part == ".")
        {
            continue;
        }
        if (part == "..")
        {
            --depth;
            if (depth < 0)
            {
                return false;
            }
            continue;
        }
        ++depth;
    }
    return depth >= 0;
}

[[nodiscard]] bool HasDuplicateDependency(const std::vector<AssetDependency>& dependencies)
{
    std::unordered_set<AssetId> seen;
    for (const AssetDependency& dependency : dependencies)
    {
        if (!seen.insert(dependency.asset_id).second)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool HasDuplicateTag(const std::vector<foundation::StringId>& tags)
{
    std::unordered_set<foundation::StringId> seen;
    for (const foundation::StringId tag : tags)
    {
        if (!seen.insert(tag).second)
        {
            return true;
        }
    }
    return false;
}
} // namespace

AssetLocation CanonicalizeAssetLocation(AssetLocation location)
{
    location.path = NormalizePath(std::move(location.path));
    return location;
}

bool IsValidAssetLocation(const AssetLocation& location) noexcept
{
    if (!IsSafeRelativePath(location.path))
    {
        return false;
    }

    if ((location.kind == AssetLocationKind::PackageEntry || location.kind == AssetLocationKind::VirtualPath) &&
        !location.mount_id.IsValid())
    {
        return false;
    }

    if (location.kind == AssetLocationKind::Generated && !location.generator_id.IsValid())
    {
        return false;
    }

    return true;
}

foundation::Result<void> InMemoryAssetCatalog::RegisterAsset(const AssetMetadata& metadata)
{
    if (sealed_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("asset.catalog_sealed", "asset catalog is sealed and cannot be mutated"));
    }

    auto normalized = ValidateAndNormalize(metadata);
    if (!normalized.HasValue())
    {
        return foundation::Result<void>::Failure(normalized.GetError());
    }

    if (assets_.contains(normalized.Value().id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("asset.duplicate", "asset id is already registered"));
    }

    assets_.emplace(normalized.Value().id, std::move(normalized).Value());
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryAssetCatalog::Seal()
{
    sealed_ = true;
    return foundation::Result<void>::Success();
}

std::optional<AssetMetadata> InMemoryAssetCatalog::FindById(AssetId id) const
{
    const auto iterator = assets_.find(id);
    if (iterator == assets_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<AssetMetadata> InMemoryAssetCatalog::FindByType(AssetType type) const
{
    std::vector<AssetMetadata> matches;
    for (const auto& [asset_id, metadata] : assets_)
    {
        (void)asset_id;
        if (metadata.type == type)
        {
            matches.push_back(metadata);
        }
    }

    std::sort(matches.begin(), matches.end(), [](const AssetMetadata& left, const AssetMetadata& right) {
        return left.id.Raw() < right.id.Raw();
    });
    return matches;
}

std::vector<AssetMetadata> InMemoryAssetCatalog::FindByTag(foundation::StringId tag) const
{
    std::vector<AssetMetadata> matches;
    for (const auto& [asset_id, metadata] : assets_)
    {
        (void)asset_id;
        for (const auto metadata_tag : metadata.tags)
        {
            if (metadata_tag == tag)
            {
                matches.push_back(metadata);
                break;
            }
        }
    }

    std::sort(matches.begin(), matches.end(), [](const AssetMetadata& left, const AssetMetadata& right) {
        return left.id.Raw() < right.id.Raw();
    });
    return matches;
}

bool InMemoryAssetCatalog::Contains(AssetId id) const
{
    return assets_.contains(id);
}

bool InMemoryAssetCatalog::IsSealed() const
{
    return sealed_;
}

foundation::Result<AssetDependencyManifest> InMemoryAssetCatalog::BuildDependencyManifest(AssetId root) const
{
    if (!root.IsValid())
    {
        return foundation::Result<AssetDependencyManifest>::Failure(
            foundation::Error::Create("asset.invalid_id", "manifest root id must be valid"));
    }

    if (!Contains(root))
    {
        return foundation::Result<AssetDependencyManifest>::Failure(
            foundation::Error::Create("asset.not_found", "manifest root asset is not registered"));
    }

    AssetDependencyManifest manifest{};
    manifest.root = root;
    std::unordered_set<AssetId> visiting;
    std::unordered_set<AssetId> visited;
    bool missing_required_dependency = false;
    if (!BuildDependencyManifestDepthFirst(root, manifest, visiting, visited, missing_required_dependency))
    {
        if (missing_required_dependency)
        {
            return foundation::Result<AssetDependencyManifest>::Failure(
                foundation::Error::Create("asset.missing_dependency", "required asset dependency is not registered"));
        }
        return foundation::Result<AssetDependencyManifest>::Failure(
            foundation::Error::Create("asset.dependency_cycle", "asset dependency graph contains a cycle"));
    }

    std::sort(manifest.dependencies.begin(), manifest.dependencies.end(), [](const AssetDependency& left, const AssetDependency& right) {
        return left.asset_id.Raw() < right.asset_id.Raw();
    });
    return foundation::Result<AssetDependencyManifest>::Success(std::move(manifest));
}

foundation::Result<AssetLocation> InMemoryAssetCatalog::Resolve(AssetId id) const
{
    const auto metadata = FindById(id);
    if (!metadata)
    {
        return foundation::Result<AssetLocation>::Failure(
            foundation::Error::Create("asset.not_found", "asset location not registered"));
    }

    return foundation::Result<AssetLocation>::Success(metadata->location);
}

foundation::Result<AssetMetadata> InMemoryAssetCatalog::ValidateAndNormalize(const AssetMetadata& metadata) const
{
    if (!metadata.id.IsValid())
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_id", "asset id must be valid before registration"));
    }

    if (!metadata.type.IsValid())
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_type", "asset type must be valid before registration"));
    }

    AssetMetadata normalized = metadata;
    normalized.location = CanonicalizeAssetLocation(normalized.location);
    if (!IsValidAssetLocation(normalized.location))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_location", "asset location must contain a valid canonical path"));
    }

    if (normalized.version == 0)
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_version", "asset metadata version must be greater than zero"));
    }

    for (const AssetDependency& dependency : normalized.dependencies)
    {
        if (!dependency.asset_id.IsValid())
        {
            return foundation::Result<AssetMetadata>::Failure(
                foundation::Error::Create("asset.invalid_dependency", "asset dependency id must be valid"));
        }

        if (dependency.asset_id == normalized.id)
        {
            return foundation::Result<AssetMetadata>::Failure(
                foundation::Error::Create("asset.self_dependency", "asset cannot depend on itself"));
        }
    }

    if (HasDuplicateDependency(normalized.dependencies))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_dependency", "duplicate asset dependencies are not allowed"));
    }

    if (HasDuplicateTag(normalized.tags))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_tag", "duplicate asset tags are not allowed"));
    }

    return foundation::Result<AssetMetadata>::Success(std::move(normalized));
}

bool InMemoryAssetCatalog::BuildDependencyManifestDepthFirst(
    AssetId current,
    AssetDependencyManifest& manifest,
    std::unordered_set<AssetId>& visiting,
    std::unordered_set<AssetId>& visited,
    bool& missing_required_dependency) const
{
    if (visited.contains(current))
    {
        return true;
    }

    if (!visiting.insert(current).second)
    {
        return false;
    }

    const auto current_metadata = FindById(current);
    if (current_metadata)
    {
        for (const AssetDependency& dependency : current_metadata->dependencies)
        {
            if (!Contains(dependency.asset_id))
            {
                if (dependency.required)
                {
                    missing_required_dependency = true;
                    return false;
                }
                if (std::none_of(manifest.dependencies.begin(), manifest.dependencies.end(), [&](const AssetDependency& existing) {
                        return existing.asset_id == dependency.asset_id;
                    }))
                {
                    manifest.dependencies.push_back(dependency);
                }
                continue;
            }
            if (std::none_of(manifest.dependencies.begin(), manifest.dependencies.end(), [&](const AssetDependency& existing) {
                    return existing.asset_id == dependency.asset_id;
                }))
            {
                manifest.dependencies.push_back(dependency);
            }
            if (!BuildDependencyManifestDepthFirst(dependency.asset_id, manifest, visiting, visited, missing_required_dependency))
            {
                return false;
            }
        }
    }

    visiting.erase(current);
    visited.insert(current);
    return true;
}
} // namespace epidemic::runtime
