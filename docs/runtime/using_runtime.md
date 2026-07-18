# Using EngineRuntime

## Minimal Composition

Use `EpidemicRuntimeSupport` when an application wants a verified default runtime composition:

```cpp
#include "Epidemic/Runtime/Support/runtime_support.h"

#include <Epidemic/Core/application.h>

int main()
{
    epidemic::core::Application app{};
    auto runtime = epidemic::runtime::RegisterDefaultEngineRuntime(app);
    if (!runtime.HasValue())
    {
        return 1;
    }

    return 0;
}
```

`RegisterDefaultEngineRuntime` performs preflight validation, creates public service bundles, validates dependencies, creates Support-owned adapters and creates an `IEngineRuntimeCoordinator`. If preflight fails, registration does not start.

The default profile is `RuntimeProfile::Reference`: deterministic reference backends are allowed. `RuntimeProfile::Production` forbids mock-only factories; with the current public Support factory this means Audio must be provided by custom composition or disabled until a production backend adapter is supplied. `RuntimeProfile::Tests` allows explicit mocks and fault-injection test backends.

Frame orchestration goes through `runtime.Value().coordinator->Tick(RuntimeFrameInput{...})`. Shutdown goes through `runtime.Value().coordinator->Shutdown()`, which is idempotent and follows `shutdown_order.md`.

## Individual Registration

Applications can register majors one by one when they need custom composition:

```cpp
epidemic::runtime::RegisterRuntimeFoundation(app);
epidemic::runtime::RegisterResources(app);
epidemic::runtime::RegisterScene(app);
epidemic::runtime::RegisterWorld(app);
epidemic::runtime::RegisterStreaming(app);
epidemic::runtime::RegisterRenderer(app);
```

Expected failures return `foundation::Result<T>`. Duplicate registration and missing dependencies are reported as failures, not exceptions.

Support registers `XxxServices` bundles, not every interface separately. Integration adapters are owned by Support/composition and may only connect public contracts listed in `update_order.md`; they must not contain gameplay logic.

Production registration uses normal service factories only. Reference and Tests profiles make deterministic reference/mock behavior explicit instead of hiding it behind production setup.

## Layer Split

EngineRuntime provides runtime systems and shared runtime state. GameFramework provides reusable gameplay mechanics. Game provides concrete rules, content and final composition.
