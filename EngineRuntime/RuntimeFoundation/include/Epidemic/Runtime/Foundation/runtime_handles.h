#pragma once

#include "Epidemic/Foundation/handle.h"

namespace epidemic::runtime
{
namespace detail
{
struct AssetHandleTag
{
};
struct ResourceHandleTag
{
};
struct RuntimeObjectHandleTag
{
};
struct SceneNodeHandleTag
{
};
} // namespace detail

using AssetHandle = foundation::Handle<detail::AssetHandleTag>;
using ResourceHandle = foundation::Handle<detail::ResourceHandleTag>;
using RuntimeObjectHandle = foundation::Handle<detail::RuntimeObjectHandleTag>;
using SceneNodeHandle = foundation::Handle<detail::SceneNodeHandleTag>;
} // namespace epidemic::runtime
