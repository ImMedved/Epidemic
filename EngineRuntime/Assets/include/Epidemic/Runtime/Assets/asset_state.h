#pragma once

namespace epidemic::runtime
{
enum class AssetState
{
    Unknown,
    Discovered,
    Indexed,
    Validated,
    Missing,
    Invalid,
    Deprecated,
};
} // namespace epidemic::runtime
