#pragma once

#include "foundation/result/result.h"

#include <memory>
#include <string_view>

namespace epidemic::layers::platform
{
class IDynamicLibrary
{
  public:
    virtual ~IDynamicLibrary() = default;

    // Human-readable library name used for diagnostics and error reporting.
    [[nodiscard]] virtual std::string_view Name() const = 0;
    // Resolves an exported symbol and returns either the address or a structured error.
    [[nodiscard]] virtual epidemic::foundation::Result<void *> FindSymbol(std::string_view symbol_name) const = 0;
};

using DynamicLibraryPtr = std::shared_ptr<IDynamicLibrary>;
} // namespace epidemic::layers::platform
