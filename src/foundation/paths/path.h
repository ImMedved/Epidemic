#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace epidemic::foundation
{
class Path
{
  public:
    Path() = default;
    explicit Path(std::filesystem::path path) : path_(std::move(path))
    {
    }

    // Parses a textual path and normalizes separators and dot segments.
    [[nodiscard]] static Path FromString(std::string_view path)
    {
        return Path(std::filesystem::path(std::string(path)).lexically_normal());
    }

    // Returns the native filesystem representation for platform calls.
    [[nodiscard]] const std::filesystem::path &Native() const noexcept
    {
        return path_;
    }

    // Reports whether the stored path is empty.
    [[nodiscard]] bool Empty() const noexcept
    {
        return path_.empty();
    }

    // Returns a forward-slash representation suitable for logs and stable comparisons.
    [[nodiscard]] std::string GenericString() const
    {
        return path_.generic_string();
    }

    // Produces a normalized copy without mutating the original instance.
    [[nodiscard]] Path LexicallyNormal() const
    {
        return Path(path_.lexically_normal());
    }

    // Appends a child segment and normalizes the resulting path.
    [[nodiscard]] Path Join(std::string_view child) const
    {
        return Path((path_ / std::filesystem::path(std::string(child))).lexically_normal());
    }

    [[nodiscard]] bool operator==(const Path &other) const noexcept
    {
        return path_ == other.path_;
    }

  private:
    std::filesystem::path path_;
};
} // namespace epidemic::foundation
