#pragma once


// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.
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


[[nodiscard]] constexpr bool IsValidAssetState(AssetState value) noexcept
{
    switch (value)
    {
    case AssetState::Unknown:
    case AssetState::Discovered:
    case AssetState::Indexed:
    case AssetState::Validated:
    case AssetState::Missing:
    case AssetState::Invalid:
    case AssetState::Deprecated:
        return true;
    }
    return false;
}
} 
