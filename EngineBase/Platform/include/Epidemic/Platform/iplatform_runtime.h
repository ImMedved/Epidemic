#pragma once

#include <Epidemic/Foundation/path.h>
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Platform/idynamic_library.h>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace epidemic::platform
{
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

    [[nodiscard]] virtual std::string_view Name() const = 0;
    [[nodiscard]] virtual const ProcessInfo &GetProcessInfo() const = 0;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point Now() const = 0;
    [[nodiscard]] virtual epidemic::foundation::Result<DynamicLibraryPtr>
    LoadDynamicLibrary(const epidemic::foundation::Path &path) = 0;
    virtual void PumpEvents() = 0;
    [[nodiscard]] virtual bool IsExitRequested() const = 0;
};
} // namespace epidemic::platform