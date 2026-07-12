#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace epidemic::foundation
{
// Native filesystem path primitive. This type is intentionally not a virtual resource path.
class Path
{
  public:
    Path() = default;

    explicit Path(std::filesystem::path path) : path_(std::move(path))
    {
    }

    [[nodiscard]] static Path FromString(std::string_view path)
    {
        return Path(std::filesystem::path(std::string(path)).lexically_normal());
    }

    [[nodiscard]] const std::filesystem::path &Native() const noexcept
    {
        return path_;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return path_.empty();
    }

    [[nodiscard]] std::string GenericString() const
    {
        return path_.generic_string();
    }

    [[nodiscard]] Path LexicallyNormal() const
    {
        return Path(path_.lexically_normal());
    }

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
} 