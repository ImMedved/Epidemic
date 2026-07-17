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

`RegisterDefaultEngineRuntime` performs preflight validation, creates public service bundles, validates dependencies and then registers the bundles in dependency order. If preflight fails, registration does not start. It is intentionally thin and does not run streaming, rendering, physics, simulation or other module behavior.

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

Production registration uses the normal service factories. Mock factories are explicit and must not be enabled by `RegisterDefaultEngineRuntime` unless the public default preset says so.

## Layer Split

EngineRuntime provides runtime systems and shared runtime state. GameFramework provides reusable gameplay mechanics. Game provides concrete rules, content and final composition.
