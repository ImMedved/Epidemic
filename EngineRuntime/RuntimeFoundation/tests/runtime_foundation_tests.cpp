#include "Epidemic/Runtime/Foundation/runtime_foundation.h"

#include <cstdint>
#include <type_traits>
#include <unordered_map>

namespace
{
using epidemic::runtime::AssetId;
using epidemic::runtime::ChunkId;
using epidemic::runtime::ResourceId;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SceneNodeHandle;
using epidemic::runtime::SceneNodeId;

bool TestDefaultInvalidIds()
{
    const AssetId asset_id{};
    const ResourceId resource_id{};
    const RuntimeObjectId runtime_object_id{};
    const SceneNodeId scene_node_id{};

    return !asset_id.IsValid() && !resource_id.IsValid() && !runtime_object_id.IsValid() && !scene_node_id.IsValid();
}

bool TestEquality()
{
    const RuntimeObjectId left{42};
    const RuntimeObjectId same{42};
    const RuntimeObjectId different{7};

    return left == same && !(left == different);
}

bool TestHashWorksInUnorderedMap()
{
    std::unordered_map<ChunkId, std::uint32_t> chunk_load_order;
    chunk_load_order.emplace(ChunkId{11}, 3u);

    const auto iterator = chunk_load_order.find(ChunkId{11});
    return iterator != chunk_load_order.end() && iterator->second == 3u;
}

bool TestAssetAndResourceIdsUseStringBackedRuntimeIds()
{
    const AssetId asset_id = AssetId::FromString("items/potato.itemdef");
    const ResourceId resource_id = ResourceId::FromString("resources/potato.mesh");

    return asset_id.IsValid() && resource_id.IsValid() && asset_id.Raw() != resource_id.Raw();
}

bool TestTypedHandlesRemainInvalidByDefault()
{
    const SceneNodeHandle handle{};
    return !handle.IsValid();
}
} // namespace

int main()
{
    static_assert(!std::is_same_v<AssetId, ResourceId>);
    static_assert(!std::is_same_v<RuntimeObjectId, SceneNodeId>);

    if (!TestDefaultInvalidIds())
    {
        return 1;
    }

    if (!TestEquality())
    {
        return 2;
    }

    if (!TestHashWorksInUnorderedMap())
    {
        return 3;
    }

    if (!TestAssetAndResourceIdsUseStringBackedRuntimeIds())
    {
        return 4;
    }

    if (!TestTypedHandlesRemainInvalidByDefault())
    {
        return 5;
    }

    return 0;
}
