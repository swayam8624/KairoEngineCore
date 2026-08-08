#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.Platform;

TEST_CASE("Platform capabilities describe the active toolchain")
{
    using namespace kairo::engine::platform;
    const auto capabilities = Capabilities();
    CHECK(capabilities.OS != OperatingSystem::Unknown);
    CHECK(capabilities.Toolchain != Compiler::Unknown);
#if defined(_WIN32)
    CHECK(capabilities.OS == OperatingSystem::Windows);
    CHECK(capabilities.NativePathSeparator == '\\');
    CHECK(capabilities.ExecutableSuffix == ".exe");
#elif defined(__APPLE__)
    CHECK(capabilities.OS == OperatingSystem::MacOS);
    CHECK(capabilities.DynamicLibrarySuffix == ".dylib");
#elif defined(__linux__)
    CHECK(capabilities.OS == OperatingSystem::Linux);
    CHECK(capabilities.DynamicLibrarySuffix == ".so");
#endif
}

TEST_CASE("Platform atomic replacement overwrites an existing file")
{
    using namespace kairo::engine::platform;
    const auto root = std::filesystem::temp_directory_path() / "kairo-platform-tests";
    std::filesystem::create_directories(root);
    const auto source = root / "source.tmp";
    const auto destination = root / "destination.txt";
    { std::ofstream stream(source, std::ios::binary); stream << "new"; }
    { std::ofstream stream(destination, std::ios::binary); stream << "old"; }
    AtomicReplaceFile(source, destination);
    CHECK_FALSE(std::filesystem::exists(source));
    std::ifstream stream(destination, std::ios::binary);
    std::string value;
    stream >> value;
    CHECK(value == "new");
    std::filesystem::remove_all(root);
}
