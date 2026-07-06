#include <Epidemic/RHI_D3D11/d3d11_rhi_device.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace epidemic::rhi::d3d11
{
namespace
{
using Microsoft::WRL::ComPtr;

[[nodiscard]] bool IsFinite(float value) noexcept
{
    return std::isfinite(value) != 0;
}

[[nodiscard]] std::string NarrowWideString(const std::wstring_view wide_string)
{
    if (wide_string.empty())
    {
        return {};
    }

    const auto buffer_size =
        WideCharToMultiByte(CP_UTF8, 0, wide_string.data(), static_cast<int>(wide_string.size()), nullptr, 0, nullptr,
                            nullptr);
    if (buffer_size <= 0)
    {
        return {};
    }

    std::string utf8_string(static_cast<std::size_t>(buffer_size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_string.data(), static_cast<int>(wide_string.size()), utf8_string.data(),
                        buffer_size, nullptr, nullptr);
    return utf8_string;
}

[[nodiscard]] std::string FormatHResult(HRESULT result)
{
    std::wstring message_buffer(512, L'\0');
    const auto message_length =
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(result), 0,
                       message_buffer.data(), static_cast<DWORD>(message_buffer.size()), nullptr);

    std::ostringstream stream;
    stream << "HRESULT=0x" << std::hex << static_cast<std::uint32_t>(result);
    if (message_length > 0)
    {
        std::wstring_view windows_message(message_buffer.data(), message_length);
        while (!windows_message.empty() &&
               (windows_message.back() == L'\r' || windows_message.back() == L'\n' || windows_message.back() == L' '))
        {
            windows_message.remove_suffix(1);
        }

        const auto utf8_message = NarrowWideString(windows_message);
        if (!utf8_message.empty())
        {
            stream << ", message=" << utf8_message;
        }
    }

    return stream.str();
}

[[nodiscard]] epidemic::foundation::Error MakeD3D11Error(std::string_view error_code, std::string_view operation,
                                                         HRESULT result, std::string_view error_context = {})
{
    return epidemic::foundation::Error::Create(error_code,
                                               "backend=D3D11, operation=" + std::string(operation) + " failed (" +
                                                   FormatHResult(result) + ")",
                                               error_context);
}

[[nodiscard]] epidemic::foundation::Result<DXGI_FORMAT> ToDxgiFormat(RhiPixelFormat pixel_format)
{
    switch (pixel_format)
    {
    case RhiPixelFormat::R8G8B8A8_UNorm:
        return epidemic::foundation::Result<DXGI_FORMAT>::Success(DXGI_FORMAT_R8G8B8A8_UNORM);
    case RhiPixelFormat::B8G8R8A8_UNorm:
        return epidemic::foundation::Result<DXGI_FORMAT>::Success(DXGI_FORMAT_B8G8R8A8_UNORM);
    case RhiPixelFormat::Unknown:
        break;
    }

    return epidemic::foundation::Result<DXGI_FORMAT>::Failure(
        epidemic::foundation::Error::Create("rhi.invalid_color_format",
                                            "D3D11 backend requires a known DXGI-compatible color format"));
}

class D3D11RhiSwapChain;

struct D3D11DeviceState
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate_context;
    D3D_FEATURE_LEVEL feature_level{D3D_FEATURE_LEVEL_10_0};
    std::weak_ptr<D3D11RhiSwapChain> active_swap_chain;
};

class D3D11RhiSwapChain final : public IRhiSwapChain, public std::enable_shared_from_this<D3D11RhiSwapChain>
{
  public:
    static epidemic::foundation::Result<std::shared_ptr<D3D11RhiSwapChain>>
    Create(std::shared_ptr<D3D11DeviceState> state, const RhiSwapChainDesc &descriptor)
    {
        auto swap_chain = std::shared_ptr<D3D11RhiSwapChain>(new D3D11RhiSwapChain(std::move(state), descriptor));
        const auto initialize_result = swap_chain->Initialize();
        if (!initialize_result.HasValue())
        {
            return epidemic::foundation::Result<std::shared_ptr<D3D11RhiSwapChain>>::Failure(initialize_result.GetError());
        }

        return epidemic::foundation::Result<std::shared_ptr<D3D11RhiSwapChain>>::Success(std::move(swap_chain));
    }

    ~D3D11RhiSwapChain() override
    {
        ReleaseBackBufferResources();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Present() override
    {
        if (swap_chain_ == nullptr || width_ == 0 || height_ == 0 || render_target_view_ == nullptr)
        {
            return epidemic::foundation::Result<void>::Success();
        }

        const auto sync_interval = descriptor_.vsync ? 1u : 0u;
        const auto result = swap_chain_->Present(sync_interval, 0);
        if (FAILED(result))
        {
            return epidemic::foundation::Result<void>::Failure(
                MakeD3D11Error("rhi.d3d11.present_failed", "IDXGISwapChain::Present", result, descriptor_.debug_name));
        }

        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Resize(std::uint32_t width, std::uint32_t height) override
    {
        if (width == 0 || height == 0)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("rhi.invalid_swap_chain_size",
                                                    "Swap chain resize dimensions must be greater than zero",
                                                    descriptor_.debug_name));
        }

        descriptor_.width = width;
        descriptor_.height = height;
        width_ = width;
        height_ = height;

        if (swap_chain_ == nullptr)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("rhi.d3d11.swap_chain_missing",
                                                    "D3D11 swap chain is not initialized", descriptor_.debug_name));
        }

        ReleaseBackBufferResources();

        const auto dxgi_format_result = ToDxgiFormat(descriptor_.color_format);
        if (!dxgi_format_result.HasValue())
        {
            return epidemic::foundation::Result<void>::Failure(dxgi_format_result.GetError());
        }

        const auto result =
            swap_chain_->ResizeBuffers(descriptor_.buffer_count, width, height, dxgi_format_result.Value(), 0);
        if (FAILED(result))
        {
            return epidemic::foundation::Result<void>::Failure(
                MakeD3D11Error("rhi.d3d11.resize_failed", "IDXGISwapChain::ResizeBuffers", result, descriptor_.debug_name));
        }

        return CreateBackBufferResources();
    }

    [[nodiscard]] std::uint32_t Width() const noexcept override
    {
        return width_;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override
    {
        return height_;
    }

    [[nodiscard]] std::uint32_t BufferCount() const noexcept override
    {
        return descriptor_.buffer_count;
    }

    [[nodiscard]] RhiPixelFormat ColorFormat() const noexcept override
    {
        return descriptor_.color_format;
    }

    [[nodiscard]] bool HasRenderTarget() const noexcept
    {
        return render_target_view_ != nullptr && width_ > 0 && height_ > 0;
    }

    void BindForRendering()
    {
        if (!HasRenderTarget())
        {
            return;
        }

        ID3D11RenderTargetView *render_target = render_target_view_.Get();
        state_->immediate_context->OMSetRenderTargets(1, &render_target, nullptr);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        state_->immediate_context->RSSetViewports(1, &viewport);
    }

    void ClearRenderTarget(const RhiColor &color)
    {
        if (!HasRenderTarget())
        {
            return;
        }

        const float clear_color[4]{color.red, color.green, color.blue, color.alpha};
        state_->immediate_context->ClearRenderTargetView(render_target_view_.Get(), clear_color);
    }

  private:
    D3D11RhiSwapChain(std::shared_ptr<D3D11DeviceState> state, RhiSwapChainDesc descriptor)
        : state_(std::move(state)), descriptor_(std::move(descriptor)), width_(descriptor_.width), height_(descriptor_.height)
    {
    }

    [[nodiscard]] epidemic::foundation::Result<void> Initialize()
    {
        const auto validation_result = Validate(descriptor_);
        if (!validation_result.HasValue())
        {
            return validation_result;
        }

        const auto dxgi_format_result = ToDxgiFormat(descriptor_.color_format);
        if (!dxgi_format_result.HasValue())
        {
            return epidemic::foundation::Result<void>::Failure(dxgi_format_result.GetError());
        }

        const auto hwnd = descriptor_.surface_handle.As<HWND>();
        if (hwnd == nullptr)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("rhi.invalid_surface_handle",
                                                    "D3D11 swap chain requires a valid Win32 window handle",
                                                    descriptor_.debug_name));
        }

        ComPtr<IDXGIDevice> dxgi_device;
        auto result = state_->device.As(&dxgi_device);
        if (FAILED(result))
        {
            return epidemic::foundation::Result<void>::Failure(
                MakeD3D11Error("rhi.d3d11.get_dxgi_device_failed", "ID3D11Device::QueryInterface(IDXGIDevice)", result,
                               descriptor_.debug_name));
        }

        ComPtr<IDXGIAdapter> adapter;
        result = dxgi_device->GetAdapter(&adapter);
        if (FAILED(result))
        {
            return epidemic::foundation::Result<void>::Failure(
                MakeD3D11Error("rhi.d3d11.get_adapter_failed", "IDXGIDevice::GetAdapter", result, descriptor_.debug_name));
        }

        ComPtr<IDXGIFactory> factory;
        result = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(result))
        {
            return epidemic::foundation::Result<void>::Failure(
                MakeD3D11Error("rhi.d3d11.get_factory_failed", "IDXGIAdapter::GetParent(IDXGIFactory)", result,
                               descriptor_.debug_name));
        }

        DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
        swap_chain_desc.BufferDesc.Width = descriptor_.width;
        swap_chain_desc.BufferDesc.Height = descriptor_.height;
        swap_chain_desc.BufferDesc.Format = dxgi_format_result.Value();
        swap_chain_desc.SampleDesc.Count = 1;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.BufferCount = descriptor_.buffer_count;
        swap_chain_desc.OutputWindow = hwnd;
        swap_chain_desc.Windowed = TRUE;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        result = factory->CreateSwapChain(state_->device.Get(), &swap_chain_desc, &swap_chain_);
        if (FAILED(result))
        {
            return epidemic::foundation::Result<void>::Failure(
                MakeD3D11Error("rhi.d3d11.create_swap_chain_failed", "IDXGIFactory::CreateSwapChain", result,
                               descriptor_.debug_name));
        }

        return CreateBackBufferResources();
    }

    [[nodiscard]] epidemic::foundation::Result<void> CreateBackBufferResources()
    {
        if (swap_chain_ == nullptr || width_ == 0 || height_ == 0)
        {
            return epidemic::foundation::Result<void>::Success();
        }

        ComPtr<ID3D11Texture2D> back_buffer;
        auto result = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (FAILED(result))
        {
            return epidemic::foundation::Result<void>::Failure(
                MakeD3D11Error("rhi.d3d11.get_back_buffer_failed", "IDXGISwapChain::GetBuffer", result,
                               descriptor_.debug_name));
        }

        ComPtr<ID3D11RenderTargetView> render_target_view;
        result = state_->device->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_view);
        if (FAILED(result))
        {
            return epidemic::foundation::Result<void>::Failure(
                MakeD3D11Error("rhi.d3d11.create_render_target_failed", "ID3D11Device::CreateRenderTargetView", result,
                               descriptor_.debug_name));
        }

        back_buffer_ = std::move(back_buffer);
        render_target_view_ = std::move(render_target_view);
        return epidemic::foundation::Result<void>::Success();
    }

    void ReleaseBackBufferResources()
    {
        if (state_ && state_->immediate_context)
        {
            ID3D11RenderTargetView *null_render_target = nullptr;
            state_->immediate_context->OMSetRenderTargets(1, &null_render_target, nullptr);
            state_->immediate_context->Flush();
        }

        render_target_view_.Reset();
        back_buffer_.Reset();
    }

    std::shared_ptr<D3D11DeviceState> state_;
    RhiSwapChainDesc descriptor_;
    std::uint32_t width_{};
    std::uint32_t height_{};
    ComPtr<IDXGISwapChain> swap_chain_;
    ComPtr<ID3D11Texture2D> back_buffer_;
    ComPtr<ID3D11RenderTargetView> render_target_view_;
};

class D3D11RhiCommandContext final : public IRhiCommandContext
{
  public:
    explicit D3D11RhiCommandContext(std::shared_ptr<D3D11DeviceState> state) : state_(std::move(state))
    {
    }

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

        const auto swap_chain = state_->active_swap_chain.lock();
        if (!swap_chain)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("rhi.d3d11.no_swap_chain",
                                                    "D3D11 command context requires an active swap chain before Clear"));
        }

        swap_chain->BindForRendering();
        swap_chain->ClearRenderTarget(clear_desc.color);
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

        ID3D11RenderTargetView *null_render_target = nullptr;
        state_->immediate_context->OMSetRenderTargets(1, &null_render_target, nullptr);
        frame_active_ = false;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] bool IsFrameActive() const noexcept override
    {
        return frame_active_;
    }

  private:
    std::shared_ptr<D3D11DeviceState> state_;
    bool frame_active_{false};
};

class D3D11RhiDevice final : public IRhiDevice
{
  public:
    D3D11RhiDevice(RhiDeviceDesc descriptor, std::shared_ptr<D3D11DeviceState> state)
        : descriptor_(std::move(descriptor)), state_(std::move(state))
    {
    }

    [[nodiscard]] std::string_view BackendName() const noexcept override
    {
        return "D3D11";
    }

    [[nodiscard]] const RhiDeviceDesc &Descriptor() const noexcept override
    {
        return descriptor_;
    }

    [[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiCommandContext>> CreateCommandContext() override
    {
        return epidemic::foundation::Result<std::shared_ptr<IRhiCommandContext>>::Success(
            std::make_shared<D3D11RhiCommandContext>(state_));
    }

    [[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiSwapChain>>
    CreateSwapChain(const RhiSwapChainDesc &swap_chain_desc) override
    {
        const auto swap_chain_result = D3D11RhiSwapChain::Create(state_, swap_chain_desc);
        if (!swap_chain_result.HasValue())
        {
            return epidemic::foundation::Result<std::shared_ptr<IRhiSwapChain>>::Failure(swap_chain_result.GetError());
        }

        const auto swap_chain = swap_chain_result.Value();
        state_->active_swap_chain = swap_chain;
        return epidemic::foundation::Result<std::shared_ptr<IRhiSwapChain>>::Success(swap_chain);
    }

  private:
    RhiDeviceDesc descriptor_;
    std::shared_ptr<D3D11DeviceState> state_;
};
} // namespace

epidemic::foundation::Result<std::shared_ptr<IRhiDevice>> CreateD3D11RhiDevice(const RhiDeviceDesc &device_desc)
{
    const auto validation_result = Validate(device_desc);
    if (!validation_result.HasValue())
    {
        return epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>::Failure(validation_result.GetError());
    }

    auto state = std::make_shared<D3D11DeviceState>();

    UINT device_flags = 0;
    if (device_desc.enable_debug_validation)
    {
        device_flags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    std::vector<D3D_FEATURE_LEVEL> feature_levels{
#ifdef D3D_FEATURE_LEVEL_11_1
        D3D_FEATURE_LEVEL_11_1,
#endif
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_flags, feature_levels.data(),
                                    static_cast<UINT>(feature_levels.size()), D3D11_SDK_VERSION, &state->device,
                                    &state->feature_level, &state->immediate_context);

#ifdef D3D_FEATURE_LEVEL_11_1
    if (result == E_INVALIDARG)
    {
        feature_levels.erase(feature_levels.begin());
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_flags, feature_levels.data(),
                                   static_cast<UINT>(feature_levels.size()), D3D11_SDK_VERSION, &state->device,
                                   &state->feature_level, &state->immediate_context);
    }
#endif

    if (FAILED(result))
    {
        return epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>::Failure(
            MakeD3D11Error("rhi.d3d11.create_device_failed", "D3D11CreateDevice", result, device_desc.debug_name));
    }

    return epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>::Success(
        std::make_shared<D3D11RhiDevice>(device_desc, std::move(state)));
}
} // namespace epidemic::rhi::d3d11