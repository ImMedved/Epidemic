#include "layers/platform/placeholders/windows_platform_runtime_stub.h"

namespace epidemic::layers::platform
{
std::string_view WindowsPlatformRuntimeStub::Name() const
{
    return "WindowsPlatformRuntimeStub";
}

void WindowsPlatformRuntimeStub::PumpEvents()
{
}

bool WindowsPlatformRuntimeStub::IsExitRequested() const
{
    return false;
}
} // namespace epidemic::layers::platform
