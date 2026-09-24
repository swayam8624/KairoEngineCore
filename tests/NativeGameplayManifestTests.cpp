#include <filesystem>
#include <memory>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.NativeGameplay;
import Kairo.EngineCore.NativeGameplayManifest;
import Kairo.EngineCore.Scene;
import Kairo.Foundation.Math;

namespace
{
    class ManifestSystem final : public kairo::engine::NativeGameplaySystem
    {
    public:
        void ApplyProperty(std::string_view, const kairo::engine::NativeGameplayValue&) override {}
    };

    kairo::engine::NativeGameplayRegistry MakeRegistry()
    {
        using namespace kairo::engine;
        NativeGameplayRegistry registry;
        NativeGameplayTypeInfo type;
        type.TypeName = "Mover";
        type.Properties = {
            {
                .Name = "enabled",
                .Type = NativeGameplayPropertyType::Boolean,
                .DefaultValue = true,
                .Exposed = true,
                .Minimum = {},
                .Maximum = {}
            },
            {
                .Name = "speed",
                .Type = NativeGameplayPropertyType::Number,
                .DefaultValue = 1.0,
                .Exposed = true,
                .Minimum = 0.0,
                .Maximum = 100.0
            },
            {
                .Name = "label",
                .Type = NativeGameplayPropertyType::String,
                .DefaultValue = std::string("player"),
                .Exposed = true,
                .Minimum = {},
                .Maximum = {}
            },
            {
                .Name = "offset",
                .Type = NativeGameplayPropertyType::Vector3,
                .DefaultValue = kairo::foundation::math::Vec3d{ 0.0, 0.0, 0.0 },
                .Exposed = true,
                .Minimum = {},
                .Maximum = {}
            },
            {
                .Name = "target",
                .Type = NativeGameplayPropertyType::EntityReference,
                .DefaultValue = Entity{},
                .Exposed = true,
                .Minimum = {},
                .Maximum = {}
            }
        };
        registry.Register(type, [] { return std::make_unique<ManifestSystem>(); });
        return registry;
    }
}

TEST_CASE("native gameplay manifest round trips all reflected property types deterministically")
{
    using namespace kairo::engine;
    Scene scene;
    const Entity actor = scene.CreateEntity("Actor");
    const Entity target = scene.CreateEntity("Target");

    NativeGameplayManifest manifest;
    NativeGameplayAttachment attachment;
    attachment.Target = actor;
    attachment.TypeName = "Mover";
    attachment.Enabled = true;
    attachment.Properties["enabled"] = false;
    attachment.Properties["speed"] = 4.25;
    attachment.Properties["label"] = std::string("hero one");
    attachment.Properties["offset"] = kairo::foundation::math::Vec3d{1.0,2.0,3.0};
    attachment.Properties["target"] = target;
    manifest.Attachments.push_back(std::move(attachment));

    const auto registry = MakeRegistry();
    ValidateNativeGameplayManifest(manifest, scene, &registry);
    const std::string first = SerializeNativeGameplayManifest(manifest);
    const auto restored = ParseNativeGameplayManifest(first);
    ValidateNativeGameplayManifest(restored, scene, &registry);
    CHECK(SerializeNativeGameplayManifest(restored) == first);
    REQUIRE(restored.Attachments.size() == 1u);
    CHECK(restored.Attachments[0].Target == actor);
    CHECK(std::get<double>(restored.Attachments[0].Properties.at("speed")) == 4.25);
    CHECK(std::get<std::string>(restored.Attachments[0].Properties.at("label")) == "hero one");
    CHECK(std::get<Entity>(restored.Attachments[0].Properties.at("target")) == target);
}

TEST_CASE("native gameplay manifest rejects unknown and mistyped reflected overrides")
{
    using namespace kairo::engine;
    Scene scene;
    const Entity actor = scene.CreateEntity("Actor");
    const auto registry = MakeRegistry();

    NativeGameplayManifest unknown;
    unknown.Attachments.push_back({ actor, "Mover", true, { { "missing", 1.0 } } });
    CHECK_THROWS_AS(ValidateNativeGameplayManifest(unknown, scene, &registry), std::invalid_argument);

    NativeGameplayManifest mistyped;
    mistyped.Attachments.push_back({ actor, "Mover", true, { { "speed", std::string("fast") } } });
    CHECK_THROWS_AS(ValidateNativeGameplayManifest(mistyped, scene, &registry), std::invalid_argument);
}

TEST_CASE("native gameplay manifest saves and reopens through atomic project text IO")
{
    using namespace kairo::engine;
    Scene scene;
    const Entity actor = scene.CreateEntity("Actor");
    NativeGameplayManifest manifest;
    manifest.Attachments.push_back({ actor, "Mover", true, { { "speed", 2.0 } } });

    const auto root = std::filesystem::temp_directory_path() / "kairo-native-gameplay-manifest-test";
    std::filesystem::remove_all(root);
    const auto path = root / DefaultNativeGameplayManifestPath;
    SaveNativeGameplayManifest(path, manifest);
    REQUIRE(std::filesystem::exists(path));
    const auto restored = LoadNativeGameplayManifest(path);
    CHECK(SerializeNativeGameplayManifest(restored) == SerializeNativeGameplayManifest(manifest));
    std::filesystem::remove_all(root);
}
