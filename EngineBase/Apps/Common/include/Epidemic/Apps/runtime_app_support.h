#pragma once

#include <Epidemic/EngineBase/engine_base_support.h>

namespace epidemic::apps
{
using CoreServicesOptions = enginebase::EngineBaseOptions;
using FramePlatformEvents = enginebase::FramePlatformEvents;
using GraphicsBackend = enginebase::GraphicsBackend;
using GraphicsRuntimeOptions = enginebase::GraphicsRuntimeOptions;
using GraphicsRuntimeServices = enginebase::GraphicsRuntimeServices;

using enginebase::CreateMainWindow;
using enginebase::EnsureFramePlatformEvents;
using enginebase::RegisterFrameThrottle;
using enginebase::RegisterGraphicsRuntime;
using enginebase::RegisterInputFrameLoop;
using enginebase::RegisterMainSwapChain;
using enginebase::RegisterRhiFrameLoop;
using enginebase::RegisterPlatformFrameLoop;

[[nodiscard]] inline std::shared_ptr<diagnostics::ILogger>
RegisterCoreRuntimeServices(core::Application &application, CoreServicesOptions options)
{
    return enginebase::RegisterEngineBase(application, std::move(options));
}

[[nodiscard]] inline std::shared_ptr<platform::IPlatformRuntime> RegisterWindowsPlatformServices(core::Application &application)
{
    return enginebase::RegisterWindowsRuntime(application);
}

[[nodiscard]] inline std::shared_ptr<input::IInputSystem> RegisterInputServices(core::Application &application)
{
    return enginebase::RegisterInputRuntime(application);
}

[[nodiscard]] inline std::shared_ptr<FramePlatformEvents> RegisterFramePlatformEvents(core::Application &application)
{
    return enginebase::EnsureFramePlatformEvents(application);
}
} // namespace epidemic::apps