#pragma once

#include "foundation/paths/path.h"

namespace epidemic::layers::runtime
{
class IVirtualFileSystem
{
  public:
    virtual ~IVirtualFileSystem() = default;

    virtual bool Mount(const epidemic::foundation::Path &root) = 0;
    [[nodiscard]] virtual bool Exists(const epidemic::foundation::Path &path) const = 0;
};
} // namespace epidemic::layers::runtime
