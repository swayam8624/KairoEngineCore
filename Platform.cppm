module;
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#if defined(_WIN32)
#include <windows.h>
#endif
export module Kairo.EngineCore.Platform;
export namespace kairo::engine::platform
{
    enum class Family { Windows, MacOS, Linux, Unknown };
    [[nodiscard]] constexpr Family CurrentFamily() noexcept
    {
    #if defined(_WIN32)
        return Family::Windows;
    #elif defined(__APPLE__)
        return Family::MacOS;
    #elif defined(__linux__)
        return Family::Linux;
    #else
        return Family::Unknown;
    #endif
    }
    [[nodiscard]] constexpr char NativePathSeparator() noexcept { return CurrentFamily()==Family::Windows?'\\':'/'; }
    [[nodiscard]] constexpr std::string_view ExecutableSuffix() noexcept { return CurrentFamily()==Family::Windows?std::string_view{".exe"}:std::string_view{}; }
    [[nodiscard]] inline std::optional<std::string> Environment(std::string_view name)
    {
        if(name.empty()) return std::nullopt;
        const std::string key(name);
    #if defined(_WIN32)
        size_t required=0;
        if(getenv_s(&required,nullptr,0,key.c_str())!=0||required==0) return std::nullopt;
        std::string value(required,'\0'); size_t written=0;
        if(getenv_s(&written,value.data(),value.size(),key.c_str())!=0||written==0) return std::nullopt;
        if(!value.empty()&&value.back()=='\0') value.pop_back();
        return value;
    #else
        const char* value=std::getenv(key.c_str());
        return value==nullptr?std::nullopt:std::optional<std::string>{value};
    #endif
    }
    inline void ReplaceFile(const std::filesystem::path& source,const std::filesystem::path& destination)
    {
    #if defined(_WIN32)
        if(!::MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::filesystem::filesystem_error("atomic file replacement failed",source,destination,std::error_code(static_cast<int>(::GetLastError()),std::system_category()));
    #else
        std::error_code error; std::filesystem::rename(source,destination,error);
        if(error) throw std::filesystem::filesystem_error("atomic file replacement failed",source,destination,error);
    #endif
    }
}
