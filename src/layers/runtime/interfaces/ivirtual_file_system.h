#pragma once

#include "foundation/paths/path.h"

namespace epidemic::layers::runtime
{
class IVirtualFileSystem
{
  public:
    virtual ~IVirtualFileSystem() = default;

    // Attaches a content root to the virtual file system.
    virtual bool Mount(const epidemic::foundation::Path &root) = 0;
    // Checks whether a virtual path is available through the mounted sources.
    [[nodiscard]] virtual bool Exists(const epidemic::foundation::Path &path) const = 0;
};
} // namespace epidemic::layers::runtime
