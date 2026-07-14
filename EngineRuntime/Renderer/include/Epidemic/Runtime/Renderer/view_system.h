#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Renderer/render_types.h"

namespace epidemic::runtime::renderer
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IViewSystem
{
  public:
    virtual ~IViewSystem() = default;

    [[nodiscard]] virtual foundation::Result<ViewId> CreateView(const ViewDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyView(ViewId view) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetMainView(ViewId view) = 0;
    [[nodiscard]] virtual ViewId GetMainView() const = 0;
    [[nodiscard]] virtual ViewLifecycle GetViewLifecycle(ViewId view) const = 0;
};
} // namespace epidemic::runtime::renderer
