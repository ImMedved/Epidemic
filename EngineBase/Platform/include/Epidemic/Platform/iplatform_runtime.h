#pragma once

#include <Epidemic/Foundation/path.h>
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Foundation/time.h>
#include <Epidemic/Platform/idynamic_library.h>

#include <string>
#include <string_view>
#include <vector>

namespace epidemic::platform
{
// This file defines the abstract runtime services provided by the host platform layer.
// IPlatformRuntime covers process metadata, clock access, dynamic library loading, and event pumping.

struct ProcessInfo
{
    epidemic::foundation::Path executable_path;
    epidemic::foundation::Path working_directory;
    std::vector<std::string> arguments;
};

class IPlatformRuntime
{
  public:
    virtual ~IPlatformRuntime() = default;

    // Returns the human-readable backend name for this runtime implementation.
    [[nodiscard]] virtual std::string_view Name() const = 0;

    // Returns immutable process information captured by the runtime.
    [[nodiscard]] virtual const ProcessInfo &GetProcessInfo() const = 0;

    // Returns the runtime's current notion of wall-clock time.
    [[nodiscard]] virtual epidemic::foundation::TimePoint Now() const = 0;

    // Loads a dynamic library from disk and returns either the library object or an Error.
    [[nodiscard]] virtual epidemic::foundation::Result<DynamicLibraryPtr>
    LoadDynamicLibrary(const epidemic::foundation::Path &path) = 0;

    // Pumps native platform messages and updates internal runtime/window state.
    virtual void PumpEvents() = 0;

    // Returns whether the runtime has observed a process-level exit request.
    [[nodiscard]] virtual bool IsExitRequested() const = 0;
};
} // namespace epidemic::platform