# Using EngineBase

This file shows how future executables and upper layers should use `EngineBase`.

The model is similar to using a framework such as Spring: the lower framework is not modified for every application. Instead, the application creates a composition root, registers framework services, adds its own modules, then starts the lifecycle.

In C++, this is explicit. There is no reflection, component scanning, or annotation-based injection. Registration is done through functions and services.

## Minimal Headless Runtime

```cpp
#include <Epidemic/Core/application.h>
#include <Epidemic/EngineBase/engine_base_support.h>

int main()
{
    epidemic::core::Application app({
        .application_name = "HeadlessRuntime",
        .frame_limit = 3,
    });

    epidemic::enginebase::RegisterEngineBase(app, {
        .runtime_name = "HeadlessRuntime",
        .log_module = "Headless",
        .worker_count = 1,
    });

    app.Bootstrap();
    app.Initialize();
    return app.Run();
}
```

`RegisterEngineBase` registers the core long-lived services: logger, configuration, event bus, task scheduler, main-thread dispatcher, and memory tracker.

After successful `Initialize()`, the service container is sealed. Register long-lived services before that point.

## Window Runtime

```cpp
#include <Epidemic/Core/application.h>
#include <Epidemic/EngineBase/engine_base_support.h>

int main()
{
    epidemic::core::Application app({
        .application_name = "WindowApp",
    });

    epidemic::enginebase::RegisterEngineBase(app, {
        .runtime_name = "WindowApp",
        .log_module = "WindowApp",
        .worker_count = 1,
        .default_window_width = 1280,
        .default_window_height = 720,
    });

    epidemic::enginebase::RegisterWindowsRuntime(app);

    epidemic::platform::WindowCreateInfo window_info;
    window_info.title = "WindowApp";
    window_info.width = 1280;
    window_info.height = 720;

    auto window_result = epidemic::enginebase::CreateMainWindow(app, window_info);
    if (!window_result.HasValue())
    {
        return 1;
    }

    epidemic::enginebase::RegisterPlatformFrameLoop(app);

    app.Bootstrap();
    app.Initialize();
    return app.Run();
}
```

Window creation returns `Result`. Creating a window may fail for normal runtime reasons, so it should not be treated as a programming error.

## Input Runtime

```cpp
epidemic::enginebase::RegisterWindowsRuntime(app);
epidemic::enginebase::RegisterInputRuntime(app);

auto window_result = epidemic::enginebase::CreateMainWindow(app, window_info);
if (!window_result.HasValue())
{
    return 1;
}

epidemic::enginebase::RegisterPlatformFrameLoop(app);
epidemic::enginebase::RegisterInputFrameLoop(app);
```

Gameplay actions do not belong in `EngineBase`. Upper layers should read snapshots or input events and map them to gameplay concepts later.

## Graphics Runtime

```cpp
epidemic::enginebase::RegisterWindowsRuntime(app);

auto graphics_result = epidemic::enginebase::RegisterGraphicsRuntime(app, {
    .backend = epidemic::enginebase::GraphicsBackend::D3D11,
    .enable_debug_validation = false,
    .debug_name = "MainGraphicsDevice",
});

if (!graphics_result.HasValue())
{
    return 1;
}

auto window_result = epidemic::enginebase::CreateMainWindow(app, window_info);
if (!window_result.HasValue())
{
    return 1;
}

epidemic::rhi::RhiSwapChainDesc swap_chain_desc;
swap_chain_desc.width = window_info.width;
swap_chain_desc.height = window_info.height;
swap_chain_desc.debug_name = "MainSwapChain";

auto swap_chain_result =
    epidemic::enginebase::RegisterMainSwapChain(app, window_result.Value(), swap_chain_desc);

if (!swap_chain_result.HasValue())
{
    return 1;
}
```

The application selects the backend in the composition root. Lower layers do not decide that D3D11 must be used.

## Frame Loop Helpers

Typical smoke/runtime setup:

```cpp
auto render_paused = std::make_shared<bool>(false);
auto clear_desc = std::make_shared<epidemic::rhi::RhiClearDesc>();
clear_desc->color = {0.05f, 0.08f, 0.12f, 1.0f};

epidemic::enginebase::RegisterPlatformFrameLoop(app);
epidemic::enginebase::RegisterInputFrameLoop(app);
epidemic::enginebase::RegisterRhiFrameLoop(
    app,
    graphics_result.Value().command_context,
    swap_chain_result.Value(),
    render_paused,
    clear_desc);
```

`RegisterRhiFrameLoop` is a baseline helper for clear-screen presentation. It is not a renderer. Higher rendering systems should live in `EngineRuntime`.

## Accessing Services

Services are retrieved by interface type:

```cpp
auto logger = app.Services().Get<epidemic::diagnostics::ILogger>();
auto scheduler = app.Services().Get<epidemic::core::tasks::ITaskScheduler>();
auto dispatcher = app.Services().Get<epidemic::core::IMainThreadDispatcher>();
```

This is similar in role to asking a framework container for a bean, but it is explicit and type-based.

Do not register services after `Application::Initialize()` succeeds. The container is sealed at that point.

## Worker Tasks and Main-Thread Handoff

Use `ITaskScheduler` for background CPU work:

```cpp
auto scheduler = app.Services().Get<epidemic::core::tasks::ITaskScheduler>();

auto handle = scheduler->Schedule([] {
    // background work
}, "PrepareData");
```

Use `IMainThreadDispatcher` to return work to the main thread:

```cpp
auto dispatcher = app.Services().Get<epidemic::core::IMainThreadDispatcher>();

dispatcher->Post([] {
    // main-thread work
}, "ApplyPreparedData");
```

Main-thread tasks run during the `RunScheduledMainThreadTasks` frame phase.

## Adding a New Upper Module Later

A future `EngineRuntime` major should follow this shape:

```cpp
void RegisterResourceManager(epidemic::core::Application& app)
{
    // register module/services before app.Initialize()
}
```

The module may use `EngineBase` contracts, but `EngineBase` must not include or link against it.

Do not modify `EngineBase` just because a higher layer needs a new gameplay or renderer feature. Add that feature to the correct upper layer unless it is truly a missing runtime foundation contract.

## Error Rules

Use `Result<T>` for expected runtime failures such as window creation, graphics backend creation, swap-chain creation, present failures, resize failures, and invalid external descriptors.

Use exceptions or assertions for engine contract violations such as duplicate service registration, null services, circular module dependencies, invalid lifecycle transitions, empty handlers, and other programming errors.
