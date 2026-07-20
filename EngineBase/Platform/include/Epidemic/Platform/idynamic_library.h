#pragma once

#include <Epidemic/Foundation/result.h>

#include <memory>
#include <string_view>

namespace epidemic::platform
{
// This file defines the minimal dynamic-library abstraction returned by the platform runtime.
// The abstraction keeps symbol lookup behind Result-based error reporting instead of exceptions.

class IDynamicLibrary
{
  public:
    virtual ~IDynamicLibrary() = default;

    // Returns the library name or origin label used for diagnostics.
    [[nodiscard]] virtual std::string_view Name() const = 0;

    // Resolves a symbol and returns either its address or an Error describing the failure.
    [[nodiscard]] virtual epidemic::foundation::Result<void *> FindSymbol(std::string_view symbol_name) const = 0;
};

using DynamicLibraryPtr = std::shared_ptr<IDynamicLibrary>;
} // namespace epidemic::platform