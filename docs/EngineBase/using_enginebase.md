# Использование EngineBase

## Composition root

Исполняемый файл создает `core::Application`, регистрирует необходимые сервисы до `Initialize()` и запускает lifecycle. EngineBase не сканирует проект и не ищет реализации автоматически.

```cpp
#include <Epidemic/Core/application.h>
#include <Epidemic/EngineBase/engine_base_support.h>

int main()
{
    epidemic::core::Application app({
        .application_name = "EpidemicGame",
    });

    epidemic::enginebase::RegisterEngineBase(app, {
        .runtime_name = "EpidemicGame",
        .log_module = "Game",
        .worker_count = 4,
        .memory_tracking_enabled = true,
    });

    app.Bootstrap();
    app.Initialize();
    return app.Run();
}
```

`RegisterEngineBase()` создает logger, configuration, event bus, task scheduler, main-thread dispatcher и memory tracker. Верхний слой получает их через interfaces из `app.Services()`.

## Окно и platform loop

```cpp
epidemic::enginebase::RegisterWindowsRuntime(app);

epidemic::platform::WindowCreateInfo window_info;
window_info.title = "Epidemic";
window_info.width = 1920;
window_info.height = 1080;

auto window = epidemic::enginebase::CreateMainWindow(app, window_info);
if (!window)
{
    return 1;
}

epidemic::enginebase::RegisterPlatformFrameLoop(app);
```

Platform runtime выкачивает Win32 messages, обновляет состояние окон и публикует `PlatformEvent`. Создание окна возвращает `Result`, потому что ошибка операционной системы является нормальным runtime failure.

## Ввод

```cpp
epidemic::enginebase::RegisterInputRuntime(app);
epidemic::enginebase::RegisterInputFrameLoop(app);
```

После input phase верхний слой читает неизменяемый `InputSnapshot`:

```cpp
auto input = app.Services().Get<epidemic::input::IInputSystem>();

const auto& snapshot = input->CurrentSnapshot();
if (snapshot.keyboard.IsKeyDown(epidemic::input::KeyCode::Escape))
{
    app.RequestStop();
}
```

EngineBase знает только физические клавиши, кнопки мыши, движение указателя и колесо. Преобразование в действия `Interact`, `Attack`, `OpenInventory` или управление кораблем выполняется в GameFramework.

## Графическое устройство

```cpp
auto graphics = epidemic::enginebase::RegisterGraphicsRuntime(app, {
    .backend = epidemic::enginebase::GraphicsBackend::D3D11,
    .enable_debug_validation = true,
    .debug_name = "MainD3D11Device",
});

if (!graphics)
{
    return 1;
}

epidemic::rhi::RhiSwapChainDesc swap_desc;
swap_desc.width = window_info.width;
swap_desc.height = window_info.height;
swap_desc.debug_name = "MainSwapChain";

auto swap_chain = epidemic::enginebase::RegisterMainSwapChain(
    app,
    window.Value(),
    swap_desc);

if (!swap_chain)
{
    return 1;
}
```

RHI helper подходит для создания presentation surface и smoke rendering. Renderer из EngineRuntime использует device boundary, но не должен превращать EngineBase RHI в хранилище meshes, materials или scene data.

## Фазы кадра

Upper layer может добавить обработчик в конкретную фазу:

```cpp
app.AddFramePhaseHandler(
    epidemic::core::FramePhase::TickModules,
    [&](const epidemic::core::FrameContext& frame)
    {
        // Обновление верхнего runtime.
    },
    "EngineRuntimeTick");
```

Обработчик должен быть коротким и не блокировать главный поток тяжелой работой. CPU-задачи передаются в `ITaskScheduler`, а результат возвращается через `IMainThreadDispatcher`.

```cpp
auto scheduler =
    app.Services().Get<epidemic::core::tasks::ITaskScheduler>();

auto dispatcher =
    app.Services().Get<epidemic::core::IMainThreadDispatcher>();

scheduler->Schedule(
    [dispatcher]
    {
        auto prepared = PrepareImmutableData();

        dispatcher->Post(
            [prepared = std::move(prepared)]
            {
                CommitOnMainThread(prepared);
            },
            "CommitPreparedData");
    },
    "PrepareData");
```

Background task не изменяет `Application`, окна, RHI context или authoritative state верхнего слоя напрямую.

## Сервисы и модули

Долгоживущий сервис регистрируется по interface type до `Initialize()`:

```cpp
app.Services().RegisterInstance<IMyService>(
    std::make_shared<MyService>());
```

Модуль реализует `IModule` и объявляет зависимости в `ModuleManifest`. `Bootstrap()` регистрирует собственные сервисы, `Initialize()` проверяет готовность, `Tick()` выполняет модульную работу, `Shutdown()` освобождает ее. Для большинства EngineRuntime majors предпочтительны service bundles и coordinator, а не один огромный `IModule`.

## Ошибки

`Result<T>` используется для ожидаемых failures: окно не создалось, backend не инициализировался, swap chain не может resize или descriptor некорректен. Duplicate service, invalid lifecycle transition и circular module dependency являются нарушениями контракта программы и могут завершаться assertion или exception.

## Подключение следующего слоя

EngineRuntime подключается как отдельный набор CMake targets и использует только публичные headers EngineBase. EngineBase не включает Runtime и не знает о его modules. Composition root сначала регистрирует EngineBase, затем подготавливает EngineRuntime, после чего запускает `Application`.

Обобщенная схема:

```cpp
RegisterEngineBase(app, base_options);
RegisterWindowsRuntime(app);
RegisterInputRuntime(app);
CreateMainWindow(app, window_info);
RegisterGraphicsRuntime(app, graphics_options);

auto runtime = PrepareEngineRuntime(runtime_options, dependencies);
CommitPreparedRuntime(app, std::move(runtime));

app.Bootstrap();
app.Initialize();
return app.Run();
```

Последние две функции относятся к завершенному Runtime Support и не должны реализовываться внутри EngineBase.
