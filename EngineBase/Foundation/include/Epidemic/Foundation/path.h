#pragma once

#include <filesystem>
#include <stdexcept>
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
    // Embedded NUL characters are rejected because downstream native filesystem APIs
    // would otherwise observe a truncated path.
    explicit Path(std::filesystem::path path) : path_(ValidateNative(std::move(path)))
    {
    }

    // Parses and lexically normalizes a string path.
    // Input: arbitrary textual path without embedded NUL characters.
    // Output: normalized Path value without hitting the filesystem.
    [[nodiscard]] static Path FromString(std::string_view path)
    {
        ValidateText(path);
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

    // Appends a relative child path and returns the normalized result.
    // Rooted or absolute children are rejected so they cannot replace the base path.
    [[nodiscard]] Path Join(std::string_view child) const
    {
        ValidateText(child);
        if (child.empty())
        {
            return *this;
        }

        const std::filesystem::path child_path{std::string(child)};
        if (child_path.has_root_path())
        {
            throw std::invalid_argument("Path::Join requires a relative child path");
        }

        return Path((path_ / child_path).lexically_normal());
    }

    // Compares two paths by their stored native representation.
    [[nodiscard]] bool operator==(const Path &other) const noexcept
    {
        return path_ == other.path_;
    }

  private:
    static void ValidateText(std::string_view value)
    {
        if (value.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument("Path text contains an embedded NUL character");
        }
    }

    [[nodiscard]] static std::filesystem::path ValidateNative(std::filesystem::path path)
    {
        const auto &native = path.native();
        using NativeChar = std::filesystem::path::value_type;
        if (native.find(NativeChar{}) != std::filesystem::path::string_type::npos)
        {
            throw std::invalid_argument("Native path contains an embedded NUL character");
        }
        return path;
    }

    std::filesystem::path path_;
};
} // namespace epidemic::foundation
