#pragma once

#include <filesystem>

namespace epidemic::layers::runtime
{
class IVirtualFileSystem
{
  public:
    virtual ~IVirtualFileSystem() = default;

    virtual bool Mount(const std::filesystem::path &root) = 0;
    [[nodiscard]] virtual bool Exists(const std::filesystem::path &path) const = 0;
};
} // namespace epidemic::layers::runtime
