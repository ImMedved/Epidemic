#include <Epidemic/Platform/windows_platform_runtime.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace epidemic::platform
{
namespace
{
[[nodiscard]] std::filesystem::path GetExecutablePath()
{
    std::vector<wchar_t> executable_buffer(MAX_PATH, L'\0');

    while (true)
    {
        const auto executable_length =
            GetModuleFileNameW(nullptr, executable_buffer.data(), static_cast<DWORD>(executable_buffer.size()));
        if (executable_length == 0)
        {
            return {};
        }

        if (executable_length < executable_buffer.size())
        {
            return std::filesystem::path(std::wstring(executable_buffer.data(), executable_length)).lexically_normal();
        }

        executable_buffer.resize(executable_buffer.size() * 2, L'\0');
    }
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

[[nodiscard]] std::string FormatWindowsErrorMessage(DWORD error_code)
{
    if (error_code == 0)
    {
        return "code=0";
    }

    std::wstring message_buffer(512, L'\0');
    const auto message_length =
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error_code, 0,
                       message_buffer.data(), static_cast<DWORD>(message_buffer.size()), nullptr);

    std::ostringstream stream;
    stream << "code=" << error_code;
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

[[nodiscard]] ProcessInfo BuildProcessInfo()
{
    ProcessInfo process_info;

    const auto executable_path = GetExecutablePath();
    if (!executable_path.empty())
    {
        process_info.executable_path = epidemic::foundation::Path(executable_path);
    }

    process_info.working_directory = epidemic::foundation::Path(std::filesystem::current_path().lexically_normal());

    int argc = 0;
    if (auto *argv = CommandLineToArgvW(GetCommandLineW(), &argc))
    {
        process_info.arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index)
        {
            process_info.arguments.push_back(NarrowWideString(argv[index]));
        }
        LocalFree(argv);
    }

    return process_info;
}

class WindowsDynamicLibrary final : public IDynamicLibrary
{
  public:
    WindowsDynamicLibrary(std::string name, HMODULE module_handle)
        : name_(std::move(name)), module_handle_(module_handle)
    {
    }

    ~WindowsDynamicLibrary() override
    {
        if (module_handle_ != nullptr)
        {
            FreeLibrary(module_handle_);
        }
    }

    [[nodiscard]] std::string_view Name() const override
    {
        return name_;
    }

    [[nodiscard]] epidemic::foundation::Result<void *> FindSymbol(std::string_view symbol_name) const override
    {
        SetLastError(ERROR_SUCCESS);
        const auto *symbol = GetProcAddress(module_handle_, std::string(symbol_name).c_str());
        if (symbol == nullptr)
        {
            const auto error_code = GetLastError();
            return epidemic::foundation::Result<void *>::Failure(
                epidemic::foundation::Error::Create(
                    "platform.symbol_not_found",
                    "Failed to resolve symbol '" + std::string(symbol_name) + "' in library '" + name_ + "' (" +
                        FormatWindowsErrorMessage(error_code) + ")"));
        }

        return epidemic::foundation::Result<void *>::Success(reinterpret_cast<void *>(symbol));
    }

  private:
    std::string name_;
    HMODULE module_handle_{nullptr};
};
} // namespace

WindowsPlatformRuntime::WindowsPlatformRuntime() : process_info_(BuildProcessInfo())
{
}

std::string_view WindowsPlatformRuntime::Name() const
{
    return "WindowsPlatformRuntime";
}

const ProcessInfo &WindowsPlatformRuntime::GetProcessInfo() const
{
    return process_info_;
}

epidemic::foundation::TimePoint WindowsPlatformRuntime::Now() const
{
    return epidemic::foundation::Clock::now();
}

epidemic::foundation::Result<DynamicLibraryPtr>
WindowsPlatformRuntime::LoadDynamicLibrary(const epidemic::foundation::Path &path)
{
    if (path.Empty())
    {
        return epidemic::foundation::Result<DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create("platform.empty_library_path", "Dynamic library path must not be empty"));
    }

    const auto module_handle = LoadLibraryW(path.Native().c_str());
    if (module_handle == nullptr)
    {
        const auto error_code = GetLastError();
        return epidemic::foundation::Result<DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create(
                "platform.load_library_failed",
                "Failed to load dynamic library '" + path.GenericString() + "' (" + FormatWindowsErrorMessage(error_code) +
                    ")"));
    }

    const auto library_name = path.GenericString().empty() ? path.Native().string() : path.GenericString();
    auto dynamic_library = std::make_shared<WindowsDynamicLibrary>(library_name, module_handle);
    return epidemic::foundation::Result<DynamicLibraryPtr>::Success(std::move(dynamic_library));
}

void WindowsPlatformRuntime::PumpEvents()
{
}

bool WindowsPlatformRuntime::IsExitRequested() const
{
    return exit_requested_;
}
} // namespace epidemic::platform
