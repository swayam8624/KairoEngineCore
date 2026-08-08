module;

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

export module Kairo.EngineCore.Platform;

export namespace kairo::engine::platform
{
    enum class OperatingSystem : unsigned char { Windows, MacOS, Linux, Unknown };
    enum class Compiler : unsigned char { MSVC, Clang, GCC, Unknown };

    struct PlatformCapabilities final
    {
        OperatingSystem OS = OperatingSystem::Unknown;
        Compiler Toolchain = Compiler::Unknown;
        bool CaseSensitiveFilesystemByDefault = true;
        bool NativeWindowingAvailable = true;
        char NativePathSeparator = '/';
        std::string_view ExecutableSuffix;
        std::string_view DynamicLibrarySuffix;
    };

    [[nodiscard]] constexpr OperatingSystem CurrentOperatingSystem() noexcept
    {
#if defined(_WIN32)
        return OperatingSystem::Windows;
#elif defined(__APPLE__) && defined(__MACH__)
        return OperatingSystem::MacOS;
#elif defined(__linux__)
        return OperatingSystem::Linux;
#else
        return OperatingSystem::Unknown;
#endif
    }

    [[nodiscard]] constexpr Compiler CurrentCompiler() noexcept
    {
#if defined(_MSC_VER)
        return Compiler::MSVC;
#elif defined(__clang__)
        return Compiler::Clang;
#elif defined(__GNUC__)
        return Compiler::GCC;
#else
        return Compiler::Unknown;
#endif
    }

    [[nodiscard]] constexpr PlatformCapabilities Capabilities() noexcept
    {
        PlatformCapabilities result;
        result.OS = CurrentOperatingSystem();
        result.Toolchain = CurrentCompiler();
        if (result.OS == OperatingSystem::Windows)
        {
            result.CaseSensitiveFilesystemByDefault = false;
            result.NativePathSeparator = '\\';
            result.ExecutableSuffix = ".exe";
            result.DynamicLibrarySuffix = ".dll";
        }
        else if (result.OS == OperatingSystem::MacOS)
        {
            result.CaseSensitiveFilesystemByDefault = false;
            result.DynamicLibrarySuffix = ".dylib";
        }
        else if (result.OS == OperatingSystem::Linux)
        {
            result.DynamicLibrarySuffix = ".so";
        }
        return result;
    }

    [[nodiscard]] inline std::optional<std::string> Environment(std::string_view name)
    {
        if (name.empty()) throw std::invalid_argument("Environment variable name cannot be empty.");
#if defined(_WIN32)
        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            name.data(), static_cast<int>(name.size()), nullptr, 0);
        if (required <= 0) throw std::runtime_error("Environment variable name is not valid UTF-8.");
        std::wstring wideName(static_cast<std::size_t>(required), L'\0');
        (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
            static_cast<int>(name.size()), wideName.data(), required);
        const DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
        if (size == 0u) return std::nullopt;
        std::wstring wideValue(size, L'\0');
        const DWORD written = GetEnvironmentVariableW(wideName.c_str(), wideValue.data(), size);
        if (written == 0u || written >= size) throw std::runtime_error("Unable to read environment variable.");
        wideValue.resize(written);
        const int utf8Size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wideValue.data(), static_cast<int>(wideValue.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Size < 0) throw std::runtime_error("Environment variable value cannot be represented as UTF-8.");
        std::string result(static_cast<std::size_t>(utf8Size), '\0');
        if (utf8Size != 0)
            (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideValue.data(),
                static_cast<int>(wideValue.size()), result.data(), utf8Size, nullptr, nullptr);
        return result;
#else
        const std::string owned(name);
        const char* value = std::getenv(owned.c_str());
        return value == nullptr ? std::nullopt : std::optional<std::string>{ value };
#endif
    }

    inline void AtomicReplaceFile(const std::filesystem::path& source,
        const std::filesystem::path& destination)
    {
        if (source.empty() || destination.empty())
            throw std::invalid_argument("Atomic file replacement requires source and destination paths.");
        if (!std::filesystem::exists(source))
            throw std::filesystem::filesystem_error("Atomic replacement source does not exist.",
                source, std::make_error_code(std::errc::no_such_file_or_directory));
#if defined(_WIN32)
        const std::wstring from = source.wstring();
        const std::wstring to = destination.wstring();
        if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::filesystem::filesystem_error("Atomic Windows file replacement failed.",
                source, destination, std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
        std::error_code error;
        std::filesystem::rename(source, destination, error);
        if (error)
            throw std::filesystem::filesystem_error("Atomic file replacement failed.", source, destination, error);
#endif
    }
}
