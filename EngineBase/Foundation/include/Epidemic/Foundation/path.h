#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace epidemic::foundation
{
// This file defines the native filesystem path wrapper used by EngineBase.
// Path is intentionally about host filesystem semantics only; it is not a virtual
// resource path, mount point, or asset identifier abstraction.

// Native filesystem path primitive.
class Path
{
  public:
    // Builds an empty path.
    Path() = default;

    // Builds a path from an already materialized std::filesystem::path.
    explicit Path(std::filesystem::path path) : path_(std::move(path))
    {
    }

    // Parses and lexically normalizes a string path.
    // Input: arbitrary textual path.
    // Output: normalized Path value without hitting the filesystem.
    [[nodiscard]] static Path FromString(std::string_view path)
    {
        return Path(std::filesystem::path(std::string(path)).lexically_normal());
    }

    // Returns the underlying native path object.
    [[nodiscard]] const std::filesystem::path &Native() const noexcept
    {
        return path_;
    }

    // Returns true when no path value is stored.
    [[nodiscard]] bool Empty() const noexcept
    {
        return path_.empty();
    }

    // Returns a generic forward-slash string form.
    // Relationship: useful for diagnostics and platform-neutral comparisons.
    [[nodiscard]] std::string GenericString() const
    {
        return path_.generic_string();
    }

    // Returns a lexically normalized copy of the path.
    [[nodiscard]] Path LexicallyNormal() const
    {
        return Path(path_.lexically_normal());
    }

    // Appends a child segment and returns the normalized result.
    [[nodiscard]] Path Join(std::string_view child) const
    {
        return Path((path_ / std::filesystem::path(std::string(child))).lexically_normal());
    }

    // Compares two paths by their stored native representation.
    [[nodiscard]] bool operator==(const Path &other) const noexcept
    {
        return path_ == other.path_;
    }

  private:
    std::filesystem::path path_;
};
} // namespace epidemic::foundation