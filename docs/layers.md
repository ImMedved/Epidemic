# Layers and Boundaries

The repository now follows the `EngineBase/` layout from `dev-log/plan.md` Step 1.

## Current module layout

```text
EngineBase/
  Foundation/
  Memory/
  Diagnostics/
  Core/
  Platform/
  Input/
  RHI/
  RHI_D3D11/
  Apps/
  Tests/
```

## Dependency intent

- `Foundation` stays at the bottom and exposes only low-level primitives.
- `Diagnostics`, `Core`, and `Platform` contain the currently implemented runtime slice.
- `Memory`, `Input`, `RHI`, and `RHI_D3D11` already exist as separate module targets so later steps can grow them without another repository move.
- Composition lives under `EngineBase/Apps/*`.
- Test entry points live under `EngineBase/Tests/*`.

## Include boundary rule

Public headers are consumed only through module include roots such as:

```cpp
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Core/application.h>
#include <Epidemic/Platform/iplatform_runtime.h>
```

No code should include another module through private source paths.