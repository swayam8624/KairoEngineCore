#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.ProjectDescriptor;

TEST_CASE("Project runtime executable round-trips and rejects traversal")
{
    using namespace kairo::engine;

    const std::string source =
        "kairo-project 2\n"
        "name \"Runtime Project\"\n"
        "engine-version \"0.1.0\"\n"
        "assets \"Assets.kassets\"\n"
        "startup-scene \"Scenes/Main.kscene\"\n"
        "input-map \"Config/Input.kinput\"\n"
        "rendering-profile \"desktop\"\n"
        "graphics-backend \"auto\"\n"
        "runtime-executable \"Build/Development/Game\"\n"
        "build-profile \"Development\" development \"Build/Development\"\n";

    const ProjectDescriptor parsed = ParseProjectDescriptor(source);
    REQUIRE(parsed.RuntimeExecutable.has_value());
    CHECK(*parsed.RuntimeExecutable == std::filesystem::path("Build/Development/Game"));

    const std::string serialized = SerializeProjectDescriptor(parsed);
    const ProjectDescriptor restored = ParseProjectDescriptor(serialized);
    REQUIRE(restored.RuntimeExecutable.has_value());
    CHECK(*restored.RuntimeExecutable == *parsed.RuntimeExecutable);

    const std::string escaping =
        "kairo-project 2\n"
        "name \"Bad Runtime\"\n"
        "engine-version \"0.1.0\"\n"
        "assets \"Assets.kassets\"\n"
        "startup-scene \"Scenes/Main.kscene\"\n"
        "input-map \"Config/Input.kinput\"\n"
        "rendering-profile \"desktop\"\n"
        "runtime-executable \"../escape\"\n"
        "build-profile \"Development\" development \"Build/Development\"\n";

    CHECK_THROWS(ParseProjectDescriptor(escaping));
}
