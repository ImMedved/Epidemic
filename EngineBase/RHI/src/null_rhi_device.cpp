#include <Epidemic/RHI/null_rhi_device.h>

#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace epidemic::rhi
{
namespace
{
[[nodiscard]] bool IsFinite(float value) noexcept
{
    return std::isfinite(value) != 0;
}

class NullRhiCommandContext final : public IRhiCommandContext
{
  public:
    [[nodiscard]] epidemic::foundation::Result<void> BeginFrame() override
    {
        if (frame_active_)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("rhi.frame_already_active",
                                                    "BeginFrame called while a frame is already active"));
        }

        frame_active_ = true;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Clear(const RhiClearDesc &clear_desc) override
    {
        if (!frame_active_)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("rhi.clear_outside_frame",
                                                    "Clear must be called between BeginFrame and EndFrame"));
        }

        const auto validation_result = Validate(clear_desc);
        if (!validation_result.HasValue())
        {
            return validation_result;
        }

        last_clear_desc_ = clear_desc;
        ++clear_count_;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> EndFrame() override
    {
        if (!frame_active_)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("rhi.no_active_frame",
                                                    "EndFrame called without an active frame"));
        }

        frame_active_ = false;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] bool IsFrameActive() const noexcept override
    {
        return frame_active_;
    }

  private:
    bool frame_active_{false};
    std::size_t clear_count_{};
    RhiClearDesc last_clear_desc_{};
};

class NullRhiSwapChain final : public IRhiSwapChain
{
  public:
    explicit NullRhiSwapChain(RhiSwapChainDesc descriptor) : descriptor_(std::move(descriptor))
    {
    }

    [[nodiscard]] epidemic::foundation::Result<void> Present() override
    {
        ++present_count_;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Resize(std::uint32_t width, std::uint32_t height) override
    {
        if (width == 0 || height == 0)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("rhi.invalid_swap_chain_size",
                                                    "Swap chain resize dimensions must be greater than zero"));
        }

        descriptor_.width = width;
        descriptor_.height = height;
        ++resize_count_;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] std::uint32_t Width() const noexcept override
    {
        return descriptor_.width;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override
    {
        return descriptor_.height;
    }

    [[nodiscard]] std::uint32_t BufferCount() const noexcept override
    {
        return descriptor_.buffer_count;
    }

    [[nodiscard]] RhiPixelFormat ColorFormat() const noexcept override
    {
        return descriptor_.color_format;
    }

  private:
    RhiSwapChainDesc descriptor_;
    std::size_t present_count_{};
    std::size_t resize_count_{};
};

class NullRhiDevice final : public IRhiDevice
{
  public:
    explicit NullRhiDevice(RhiDeviceDesc descriptor) : descriptor_(std::move(descriptor))
    {
    }

    [[nodiscard]] std::string_view BackendName() const noexcept override
    {
        return "NullRHI";
    }

    [[nodiscard]] const RhiDeviceDesc &Descriptor() const noexcept override
    {
        return descriptor_;
    }

    [[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiCommandContext>> CreateCommandContext() override
    {
        return epidemic::foundation::Result<std::shared_ptr<IRhiCommandContext>>::Success(
            std::make_shared<NullRhiCommandContext>());
    }

    [[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiSwapChain>>
    CreateSwapChain(const RhiSwapChainDesc &swap_chain_desc) override
    {
        const auto validation_result = Validate(swap_chain_desc);
        if (!validation_result.HasValue())
        {
            return epidemic::foundation::Result<std::shared_ptr<IRhiSwapChain>>::Failure(validation_result.GetError());
        }

        return epidemic::foundation::Result<std::shared_ptr<IRhiSwapChain>>::Success(
            std::make_shared<NullRhiSwapChain>(swap_chain_desc));
    }

  private:
    RhiDeviceDesc descriptor_;
};
} // namespace

epidemic::foundation::Result<void> Validate(const RhiDeviceDesc &device_desc)
{
    if (device_desc.debug_name.empty())
    {
        return epidemic::foundation::Result<void>::Failure(
            epidemic::foundation::Error::Create("rhi.empty_device_name", "RHI device debug name must not be empty"));
    }

    return epidemic::foundation::Result<void>::Success();
}

epidemic::foundation::Result<void> Validate(const RhiSwapChainDesc &swap_chain_desc)
{
    if (!swap_chain_desc.surface_handle.IsValid())
    {
        return epidemic::foundation::Result<void>::Failure(
            epidemic::foundation::Error::Create("rhi.invalid_surface_handle",
                                                "Swap chain surface handle must be valid"));
    }

    if (swap_chain_desc.width == 0 || swap_chain_desc.height == 0)
    {
        return epidemic::foundation::Result<void>::Failure(
            epidemic::foundation::Error::Create("rhi.invalid_swap_chain_size",
                                                "Swap chain dimensions must be greater than zero"));
    }

    if (swap_chain_desc.buffer_count == 0)
    {
        return epidemic::foundation::Result<void>::Failure(
            epidemic::foundation::Error::Create("rhi.invalid_buffer_count",
                                                "Swap chain buffer count must be greater than zero"));
    }

    if (swap_chain_desc.color_format == RhiPixelFormat::Unknown)
    {
        return epidemic::foundation::Result<void>::Failure(
            epidemic::foundation::Error::Create("rhi.invalid_color_format",
                                                "Swap chain color format must be specified"));
    }

    return epidemic::foundation::Result<void>::Success();
}

epidemic::foundation::Result<void> Validate(const RhiClearDesc &clear_desc)
{
    if (!clear_desc.clear_color)
    {
        return epidemic::foundation::Result<void>::Failure(
            epidemic::foundation::Error::Create("rhi.nothing_to_clear",
                                                "RHI clear descriptor must request at least one clear operation"));
    }

    if (!IsFinite(clear_desc.color.red) || !IsFinite(clear_desc.color.green) || !IsFinite(clear_desc.color.blue) ||
        !IsFinite(clear_desc.color.alpha))
    {
        return epidemic::foundation::Result<void>::Failure(
            epidemic::foundation::Error::Create("rhi.invalid_clear_color",
                                                "RHI clear color components must be finite values"));
    }

    return epidemic::foundation::Result<void>::Success();
}

epidemic::foundation::Result<std::shared_ptr<IRhiDevice>> CreateNullRhiDevice(const RhiDeviceDesc &device_desc)
{
    const auto validation_result = Validate(device_desc);
    if (!validation_result.HasValue())
    {
        return epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>::Failure(validation_result.GetError());
    }

    return epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>::Success(std::make_shared<NullRhiDevice>(device_desc));
}
} // namespace epidemic::rhi