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
    if (path.empty())
    {
        return false;
    }
    // Any rooted slash/backslash form is host-rooted (POSIX, UNC, device path, etc.).
    if (path.front() == '/' || path.front() == '\\')
    {
        return true;
    }
    // Windows drive-relative paths ("C:foo" and "C:") are not engine-root-relative.
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
}

[[nodiscard]] bool IsSafeRelativePath(std::string_view path) noexcept
{
    if (path.empty() || IsAbsolutePath(path))
    {
        return false;
    }

    int depth = 0;
    std::size_t segment_begin = 0;
    for (std::size_t index = 0; index <= path.size(); ++index)
    {
        const bool end = index == path.size();
        const bool separator = !end && (path[index] == '/' || path[index] == '\\');
        if (!end && !separator)
        {
            continue;
        }

        const std::string_view part = path.substr(segment_begin, index - segment_begin);
        segment_begin = index + 1;
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
    if (!IsValidAssetLocationKind(location.kind))
    {
        return false;
    }
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

    enum class VisitState : std::uint8_t
    {
        Visiting,
        Visited,
    };
    struct Frame
    {
        AssetId id{};
        std::size_t dependency_index = 0;
    };

    std::unordered_map<AssetId, VisitState> states;
    std::unordered_set<AssetId> manifest_ids;
    std::vector<Frame> stack;
    states.emplace(root, VisitState::Visiting);
    stack.push_back(Frame{root, 0});

    while (!stack.empty())
    {
        Frame& frame = stack.back();
        const auto metadata = FindById(frame.id);
        if (!metadata)
        {
            return foundation::Result<AssetDependencyManifest>::Failure(
                foundation::Error::Create("asset.missing_dependency", "required asset dependency is not registered"));
        }

        if (frame.dependency_index >= metadata->dependencies.size())
        {
            states[frame.id] = VisitState::Visited;
            stack.pop_back();
            continue;
        }

        const AssetDependency dependency = metadata->dependencies[frame.dependency_index++];
        const auto dependency_metadata = FindById(dependency.asset_id);
        if (!dependency_metadata)
        {
            if (dependency.required)
            {
                return foundation::Result<AssetDependencyManifest>::Failure(
                    foundation::Error::Create("asset.missing_dependency", "required asset dependency is not registered"));
            }
            if (manifest_ids.insert(dependency.asset_id).second)
            {
                manifest.dependencies.push_back(dependency);
            }
            continue;
        }

        if (manifest_ids.insert(dependency.asset_id).second)
        {
            manifest.dependencies.push_back(dependency);
        }

        const auto state = states.find(dependency.asset_id);
        if (state != states.end())
        {
            if (state->second == VisitState::Visiting)
            {
                return foundation::Result<AssetDependencyManifest>::Failure(
                    foundation::Error::Create("asset.dependency_cycle", "asset dependency graph contains a cycle"));
            }
            continue;
        }

        states.emplace(dependency.asset_id, VisitState::Visiting);
        stack.push_back(Frame{dependency.asset_id, 0});
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

    if (!IsValidAssetState(metadata.state))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_state", "asset state is outside the declared enum domain"));
    }

    if (!IsValidAssetLocationKind(metadata.location.kind))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_location", "asset location kind is outside the declared enum domain"));
    }

    if (metadata.location.path.size() > kMaxAssetPathBytes)
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.metadata_too_large", "asset location path exceeds the supported size limit"));
    }
    if (metadata.dependencies.size() > kMaxAssetDependencies)
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.metadata_too_large", "asset dependency list exceeds the supported size limit"));
    }
    if (metadata.tags.size() > kMaxAssetTags)
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.metadata_too_large", "asset tag list exceeds the supported size limit"));
    }

    if (!IsValidAssetLocation(metadata.location))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_location", "asset location must be an engine-root-relative path"));
    }

    if (metadata.version == 0)
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_version", "asset metadata version must be greater than zero"));
    }

    for (const AssetDependency& dependency : metadata.dependencies)
    {
        if (!dependency.asset_id.IsValid())
        {
            return foundation::Result<AssetMetadata>::Failure(
                foundation::Error::Create("asset.invalid_dependency", "asset dependency id must be valid"));
        }

        if (dependency.asset_id == metadata.id)
        {
            return foundation::Result<AssetMetadata>::Failure(
                foundation::Error::Create("asset.self_dependency", "asset cannot depend on itself"));
        }
    }

    for (const foundation::StringId tag : metadata.tags)
    {
        if (!tag.IsValid())
        {
            return foundation::Result<AssetMetadata>::Failure(
                foundation::Error::Create("asset.invalid_tag", "asset tag id must be valid"));
        }
    }

    if (HasDuplicateDependency(metadata.dependencies))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_dependency", "duplicate asset dependencies are not allowed"));
    }

    if (HasDuplicateTag(metadata.tags))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_tag", "duplicate asset tags are not allowed"));
    }

    AssetMetadata normalized = metadata;
    normalized.location = CanonicalizeAssetLocation(normalized.location);
    if (!IsValidAssetLocation(normalized.location))
    {
        return foundation::Result<AssetMetadata>::Failure(
            foundation::Error::Create("asset.invalid_location", "asset location must contain a valid canonical path"));
    }

    return foundation::Result<AssetMetadata>::Success(std::move(normalized));
}

} // namespace epidemic::runtime
