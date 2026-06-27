#pragma once

#include "foundation/paths/path.h"
#include "foundation/result/result.h"
#include "layers/platform/interfaces/idynamic_library.h"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace epidemic::layers::platform
{
struct ProcessInfo
{
    // Absolute path to the currently running executable.
    epidemic::foundation::Path executable_path;
    // Working directory captured during platform runtime construction.
    epidemic::foundation::Path working_directory;
    // Command-line arguments in UTF-8 form.
    std::vector<std::string> arguments;
};

class IPlatformRuntime
{
  public:
    virtual ~IPlatformRuntime() = default;

    // Returns the concrete runtime name for logging and diagnostics.
    [[nodiscard]] virtual std::string_view Name() const = 0;
    // Exposes process metadata captured from the host platform.
    [[nodiscard]] virtual const ProcessInfo &GetProcessInfo() const = 0;
    // Returns a monotonic timestamp for runtime measurements.
    [[nodiscard]] virtual std::chrono::steady_clock::time_point Now() const = 0;
    // Loads a dynamic library from disk through the platform abstraction.
    [[nodiscard]] virtual epidemic::foundation::Result<DynamicLibraryPtr>
    LoadDynamicLibrary(const epidemic::foundation::Path &path) = 0;
    // Pumps platform events once for the current headless runtime slice.
    virtual void PumpEvents() = 0;
    // Reports whether the host requested application termination.
    [[nodiscard]] virtual bool IsExitRequested() const = 0;
};
} // namespace epidemic::layers::platform
