#pragma once

#include <Epidemic/Foundation/result.h>

#include <memory>
#include <string_view>

namespace epidemic::platform
{
class IDynamicLibrary
{
  public:
    virtual ~IDynamicLibrary() = default;

    [[nodiscard]] virtual std::string_view Name() const = 0;
    [[nodiscard]] virtual epidemic::foundation::Result<void *> FindSymbol(std::string_view symbol_name) const = 0;
};

using DynamicLibraryPtr = std::shared_ptr<IDynamicLibrary>;
} // namespace epidemic::platform