#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

import Kairo.EngineCore;

using namespace kairo::engine;

TEST_CASE("Project descriptor round trips an optional Play executable",
    "[KairoEngineCore][Project]")
{
    ProjectDescriptor descriptor;
    descriptor.Name = "Playable";
    descriptor.AssetManifest = "Assets.kassets";
    descriptor.StartupScene = "Scenes/Main.kscene";
    descriptor.EngineVersion = "0.1.0";
    descriptor.InputMap = "Config/Input.kinput";
    descriptor.RenderingProfile = "desktop";
    descriptor.GraphicsBackend = "auto";
    descriptor.PlayExecutable = std::filesystem::path("Build/Development/PlayableGame");
    descriptor.BuildProfiles = {
        { "Development", ProjectBuildKind::Development, "Build/Development" },
        { "Release", ProjectBuildKind::Release, "Build/Release" }
    };

    const std::string source = SerializeProjectDescriptor(descriptor);
    CHECK(source.find("play-executable \"Build/Development/PlayableGame\"") != std::string::npos);

    const auto restored = ParseProjectDescriptor(source);
    REQUIRE(restored.PlayExecutable.has_value());
    CHECK(*restored.PlayExecutable == std::filesystem::path("Build/Development/PlayableGame"));
    CHECK(restored == descriptor);
}

TEST_CASE("Project descriptor rejects unsafe Play executable paths",
    "[KairoEngineCore][Project]")
{
    ProjectDescriptor descriptor;
    descriptor.Name = "Playable";
    descriptor.AssetManifest = "Assets.kassets";
    descriptor.StartupScene = "Scenes/Main.kscene";
    descriptor.EngineVersion = "0.1.0";
    descriptor.InputMap = "Config/Input.kinput";
    descriptor.RenderingProfile = "desktop";
    descriptor.GraphicsBackend = "auto";
    descriptor.PlayExecutable = std::filesystem::path("../outside/Game");

    REQUIRE_THROWS_AS(ValidateProjectDescriptor(descriptor), std::invalid_argument);
}
