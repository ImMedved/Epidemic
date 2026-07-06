#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/irhi_command_context.h>
#include <Epidemic/RHI/irhi_swap_chain.h>

#include <memory>
#include <string_view>

namespace epidemic::rhi
{
class IRhiDevice
{
  public:
    virtual ~IRhiDevice() = default;

    [[nodiscard]] virtual std::string_view BackendName() const noexcept = 0;
    [[nodiscard]] virtual const RhiDeviceDesc &Descriptor() const noexcept = 0;
    [[nodiscard]] virtual epidemic::foundation::Result<std::shared_ptr<IRhiCommandContext>> CreateCommandContext() = 0;
    [[nodiscard]] virtual epidemic::foundation::Result<std::shared_ptr<IRhiSwapChain>>
    CreateSwapChain(const RhiSwapChainDesc &swap_chain_desc) = 0;
};
} // namespace epidemic::rhi