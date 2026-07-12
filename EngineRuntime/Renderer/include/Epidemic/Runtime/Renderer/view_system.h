#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Renderer/render_types.h"

namespace epidemic::runtime::renderer
{
// File note:
// Public view-management contract for render camera registration and main-view selection.
class IViewSystem
{
  public:
    virtual ~IViewSystem() = default;

    [[nodiscard]] virtual foundation::Result<ViewId> CreateView(const ViewDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetMainView(ViewId view) = 0;
    [[nodiscard]] virtual ViewId GetMainView() const = 0;
};
} // namespace epidemic::runtime::renderer
