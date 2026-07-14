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

`RegisterDefaultEngineRuntime` registers composition markers in dependency order and returns a summary of registered majors. It is intentionally thin and does not run streaming, rendering, physics, simulation or other module behavior.

## Individual Registration

Applications can register majors one by one when they need custom composition:

```cpp
epidemic::runtime::RegisterRuntimeFoundation(app);
epidemic::runtime::RegisterResources(app);
epidemic::runtime::RegisterScene(app);
epidemic::runtime::RegisterRenderer(app);
```

Expected failures return `foundation::Result<T>`. Duplicate registration and missing dependencies are reported as failures, not exceptions.

## Layer Split

EngineRuntime provides runtime systems and shared runtime state. GameFramework provides reusable gameplay mechanics. Game provides concrete rules, content and final composition.
