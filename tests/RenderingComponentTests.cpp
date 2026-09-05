#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

import Kairo.EngineCore;
import Kairo.Reflection;

namespace
{
    using namespace kairo::engine;

    const auto MeshID = kairo::assets::AssetID::Parse(
        "10000000-0000-4000-8000-000000000001");
    const auto MaterialAID = kairo::assets::AssetID::Parse(
        "10000000-0000-4000-8000-000000000002");
    const auto MaterialBID = kairo::assets::AssetID::Parse(
        "10000000-0000-4000-8000-000000000003");
    const auto EnvironmentID = kairo::assets::AssetID::Parse(
        "10000000-0000-4000-8000-000000000004");
    const auto ImportedSceneID = kairo::assets::AssetID::Parse(
        "10000000-0000-4000-8000-000000000005");

    void PopulateRenderingAssets(kairo::assets::AssetRegistry& assets)
    {
        assets.Insert({ MeshID, kairo::assets::AssetType::Mesh,
            kairo::assets::AssetOrigin::SourceFile, "Meshes/test.glb", "kairo.glb", 1u, {} });
        assets.Insert({ MaterialAID, kairo::assets::AssetType::Material,
            kairo::assets::AssetOrigin::Generated, "Materials/a.kmat", "kairo.material", 1u, {} });
        assets.Insert({ MaterialBID, kairo::assets::AssetType::Material,
            kairo::assets::AssetOrigin::Generated, "Materials/b.kmat", "kairo.material", 1u, {} });
        assets.Insert({ EnvironmentID, kairo::assets::AssetType::Texture2D,
            kairo::assets::AssetOrigin::SourceFile, "Textures/environment.hdr", "kairo.stb-texture", 1u, {} });
        assets.Insert({ ImportedSceneID, kairo::assets::AssetType::Scene,
            kairo::assets::AssetOrigin::SourceFile, "Scenes/model.glb", "kairo.gltf.scene", 1u, {} });
    }
}

TEST_CASE("Rendering components enforce renderer-neutral invariants",
    "[KairoEngineCore][Rendering][Validation]")
{
    using namespace kairo::engine;

    MeshRendererComponent mesh;
    mesh.MeshAsset = { MeshID };
    mesh.MaterialAsset = { MaterialAID };
    mesh.AdditionalMaterialSlots = { { MaterialBID } };
    REQUIRE_NOTHROW(mesh.Validate());
    CHECK(mesh.MaterialSlotCount() == 2u);
    CHECK(mesh.MaterialForSlot(1u).ID == MaterialBID);
    REQUIRE_THROWS_AS(mesh.MaterialForSlot(2u), std::out_of_range);
    mesh.RenderLayers = 0u;
    REQUIRE_THROWS_AS(mesh.Validate(), std::invalid_argument);

    SceneInstanceComponent instance{ { ImportedSceneID }, true, false, true, 0x20u };
    REQUIRE_NOTHROW(instance.Validate());
    instance.RenderLayers = 0u;
    REQUIRE_THROWS_AS(instance.Validate(), std::invalid_argument);

    CameraComponent camera;
    camera.Projection = CameraProjection::Orthographic;
    camera.OrthographicSize = 24.0f;
    camera.ExposureEV100 = -2.0f;
    REQUIRE_NOTHROW(camera.Validate());
    camera.ClearColor.w = 1.1f;
    REQUIRE_THROWS_AS(camera.Validate(), std::invalid_argument);

    LightComponent light;
    REQUIRE_NOTHROW(light.Validate());
    light.Type = LightType::Point;
    REQUIRE_THROWS_AS(light.Validate(), std::invalid_argument);
    light.Unit = PhotometricUnit::Candela;
    REQUIRE_NOTHROW(light.Validate());
    light.OuterConeRadians = light.InnerConeRadians - 0.01f;
    REQUIRE_THROWS_AS(light.Validate(), std::invalid_argument);

    EnvironmentComponent environment;
    environment.EnvironmentTexture = kairo::assets::TextureAssetHandle{ EnvironmentID };
    environment.ExposureEV100 = -4.0f;
    REQUIRE_NOTHROW(environment.Validate());
    environment.FogFar = environment.FogNear;
    REQUIRE_THROWS_AS(environment.Validate(), std::invalid_argument);
}

TEST_CASE("Scene selects authored cameras lights and environments deterministically",
    "[KairoEngineCore][Rendering][Scene]")
{
    using namespace kairo::engine;
    Scene scene;
    const Entity camera = scene.CreateEntity("Camera");
    const Entity light = scene.CreateEntity("Light");
    const Entity low = scene.CreateEntity("Low Environment");
    const Entity high = scene.CreateEntity("High Environment");

    scene.SetCamera(camera, CameraComponent{ .Primary = true });
    scene.SetLight(light, {});
    scene.SetEnvironment(low, EnvironmentComponent{ .Priority = 2 });
    scene.SetEnvironment(high, EnvironmentComponent{ .Priority = 9 });

    CHECK(scene.PrimaryCamera() == camera);
    CHECK(scene.LightEntities() == std::vector<Entity>{ light });
    CHECK(scene.ActiveEnvironment() == high);
    scene.SetEnabled(high, false);
    CHECK(scene.ActiveEnvironment() == low);

    const Entity duplicate = scene.CreateEntity("Duplicate Primary");
    REQUIRE_THROWS_AS(scene.SetCamera(duplicate, CameraComponent{ .Primary = true }),
        std::invalid_argument);
    CHECK_FALSE(scene.HasCamera(duplicate));
}

TEST_CASE("Runtime scene copies preserve rendering data without mutating authoring state",
    "[KairoEngineCore][Rendering][PlayClone]")
{
    using namespace kairo::engine;
    Scene authored;
    const Entity renderable = authored.CreateEntity("Renderable");
    const Entity cameraEntity = authored.CreateEntity("Camera");
    const Entity lightEntity = authored.CreateEntity("Light");
    const Entity environmentEntity = authored.CreateEntity("Environment");

    MeshRendererComponent mesh;
    mesh.MeshAsset = { MeshID };
    mesh.MaterialAsset = { MaterialAID };
    mesh.AdditionalMaterialSlots = { { MaterialBID } };
    mesh.CastShadows = false;
    mesh.RenderLayers = 0x15u;
    authored.SetMeshRenderer(renderable, mesh);
    CameraComponent camera;
    camera.Projection = CameraProjection::Orthographic;
    camera.OrthographicSize = 12.0f;
    camera.Primary = true;
    authored.SetCamera(cameraEntity, camera);
    LightComponent light;
    light.Type = LightType::Point;
    light.Unit = PhotometricUnit::Candela;
    light.Intensity = 450.0f;
    light.Range = 30.0f;
    authored.SetLight(lightEntity, light);
    EnvironmentComponent environment;
    environment.Priority = 4;
    environment.ExposureEV100 = 1.25f;
    authored.SetEnvironment(environmentEntity, environment);

    Scene runtime = authored;
    CHECK(runtime.MeshRenderer(renderable).AdditionalMaterialSlots ==
        mesh.AdditionalMaterialSlots);
    CHECK(runtime.MeshRenderer(renderable).RenderLayers == 0x15u);
    CHECK(runtime.Camera(cameraEntity).OrthographicSize == 12.0f);
    CHECK(runtime.Light(lightEntity).Intensity == 450.0f);
    CHECK(runtime.Environment(environmentEntity).ExposureEV100 == 1.25f);

    auto runtimeMesh = runtime.MeshRenderer(renderable);
    runtimeMesh.Visible = false;
    runtime.SetMeshRenderer(renderable, runtimeMesh);
    auto runtimeEnvironment = runtime.Environment(environmentEntity);
    runtimeEnvironment.ExposureEV100 = -2.0f;
    runtime.SetEnvironment(environmentEntity, runtimeEnvironment);

    CHECK(authored.MeshRenderer(renderable).Visible);
    CHECK(authored.Environment(environmentEntity).ExposureEV100 == 1.25f);
    CHECK_FALSE(runtime.MeshRenderer(renderable).Visible);
    CHECK(runtime.Environment(environmentEntity).ExposureEV100 == -2.0f);
}

TEST_CASE("Scene V4 round trips complete rendering authoring data",
    "[KairoEngineCore][Rendering][Serialization]")
{
    using namespace kairo::engine;
    kairo::assets::AssetRegistry assets;
    PopulateRenderingAssets(assets);
    Scene source;
    const Entity renderable = source.CreateEntityWithID({ 2u }, "Renderable");
    MeshRendererComponent mesh;
    mesh.MeshAsset = { MeshID };
    mesh.MaterialAsset = { MaterialAID };
    mesh.AdditionalMaterialSlots = { { MaterialBID } };
    mesh.Visible = true;
    mesh.CastShadows = false;
    mesh.ReceiveShadows = true;
    mesh.RenderLayers = 0x12u;
    source.SetMeshRenderer(renderable, mesh);

    const Entity importedEntity = source.CreateEntityWithID({ 6u }, "Imported Scene");
    SceneInstanceComponent instance;
    instance.SceneAsset = { ImportedSceneID };
    instance.CastShadows = false;
    instance.RenderLayers = 0x78u;
    source.SetSceneInstance(importedEntity, instance);

    const Entity cameraEntity = source.CreateEntityWithID({ 3u }, "Camera");
    CameraComponent camera;
    camera.Primary = true;
    camera.Projection = CameraProjection::Orthographic;
    camera.OrthographicSize = 18.0f;
    camera.NearPlane = 0.25f;
    camera.FarPlane = 800.0f;
    camera.ExposureEV100 = -1.5f;
    camera.ClearMode = CameraClearMode::SolidColor;
    camera.ClearColor = { 0.1f, 0.2f, 0.3f, 0.75f };
    camera.RenderLayers = 0x34u;
    source.SetCamera(cameraEntity, camera);

    const Entity lightEntity = source.CreateEntityWithID({ 4u }, "Area Light");
    LightComponent light;
    light.Type = LightType::RectangleArea;
    light.Unit = PhotometricUnit::Nit;
    light.Color = { 1.0f, 0.75f, 0.5f };
    light.Intensity = 1200.0f;
    light.AreaWidth = 3.0f;
    light.AreaHeight = 2.0f;
    light.Shadows = ShadowPolicy::Soft;
    light.RenderLayers = 0x56u;
    source.SetLight(lightEntity, light);

    const Entity environmentEntity = source.CreateEntityWithID({ 5u }, "Environment");
    EnvironmentComponent environment;
    environment.Priority = 7;
    environment.BackgroundColor = { 0.02f, 0.04f, 0.08f };
    environment.EnvironmentTexture = kairo::assets::TextureAssetHandle{ EnvironmentID };
    environment.AmbientIntensity = 0.25f;
    environment.EnvironmentIntensity = 1.75f;
    environment.Fog = FogMode::Exponential;
    environment.FogColor = { 0.4f, 0.5f, 0.6f };
    environment.FogDensity = 0.02f;
    environment.FogNear = 1.0f;
    environment.FogFar = 400.0f;
    environment.ExposureEV100 = 1.0f;
    environment.ToneMap = ToneMapping::Reinhard;
    source.SetEnvironment(environmentEntity, environment);

    const std::string encoded = SerializeScene(source, assets);
    REQUIRE(encoded.starts_with("kairo-scene 4\n"));
    const Scene restored = ParseScene(encoded, assets);

    const auto& restoredMesh = restored.MeshRenderer(renderable);
    CHECK(restoredMesh.MaterialSlotCount() == 2u);
    CHECK(restoredMesh.MaterialForSlot(1u).ID == MaterialBID);
    CHECK_FALSE(restoredMesh.CastShadows);
    CHECK(restoredMesh.RenderLayers == 0x12u);
    REQUIRE(restored.HasSceneInstance(importedEntity));
    CHECK(restored.SceneInstance(importedEntity).SceneAsset.ID == ImportedSceneID);
    CHECK_FALSE(restored.SceneInstance(importedEntity).CastShadows);
    CHECK(restored.SceneInstance(importedEntity).RenderLayers == 0x78u);
    CHECK(restored.Camera(cameraEntity).Projection == CameraProjection::Orthographic);
    CHECK(restored.Camera(cameraEntity).ClearMode == CameraClearMode::SolidColor);
    CHECK(restored.Camera(cameraEntity).ExposureEV100 == -1.5f);
    CHECK(restored.Light(lightEntity).Type == LightType::RectangleArea);
    CHECK(restored.Light(lightEntity).Unit == PhotometricUnit::Nit);
    CHECK(restored.Light(lightEntity).AreaWidth == 3.0f);
    CHECK(restored.Environment(environmentEntity).EnvironmentTexture->ID == EnvironmentID);
    CHECK(restored.Environment(environmentEntity).Fog == FogMode::Exponential);
    CHECK(restored.Environment(environmentEntity).ToneMap == ToneMapping::Reinhard);
    CHECK(SerializeScene(restored, assets) == encoded);
}

TEST_CASE("Scene migration and reference failures are explicit",
    "[KairoEngineCore][Rendering][Migration]")
{
    using namespace kairo::engine;
    kairo::assets::AssetRegistry assets;
    PopulateRenderingAssets(assets);
    const std::string versionTwo =
        "kairo-scene 2\nentity 1 \"Legacy Camera\"\n"
        "camera 1.1 0.1 100 true\nend\n";
    const Scene migrated = ParseScene(versionTwo, assets);
    CHECK(migrated.Camera({ 1u }).Projection == CameraProjection::Perspective);
    CHECK(migrated.Camera({ 1u }).ClearMode == CameraClearMode::Environment);
    CHECK(SerializeScene(migrated, assets).starts_with("kairo-scene 4\n"));

    const auto missing = kairo::assets::AssetID::Parse(
        "ffffffff-ffff-4fff-8fff-ffffffffffff");
    const std::string missingMaterial =
        "kairo-scene 3\nentity 1 \"Mesh\"\nmesh-renderer " + MeshID.ToString() +
        " true true true 1 2 " + MaterialAID.ToString() + " " + missing.ToString() +
        "\nend\n";
    REQUIRE_THROWS_AS(ParseScene(missingMaterial, assets), SceneFormatError);

    const std::string missingEnvironment =
        "kairo-scene 3\nentity 1 \"World\"\nenvironment true 0 0 0 0 " +
        missing.ToString() + " 0 1 disabled 0 0 0 0.01 0 100 0 aces global\nend\n";
    REQUIRE_THROWS_AS(ParseScene(missingEnvironment, assets), SceneFormatError);

    const std::string duplicatePrimary =
        "kairo-scene 3\nentity 1 \"A\"\ncamera perspective 1 10 0.1 100 0 true environment 0 0 0 1 1\nend\n"
        "entity 2 \"B\"\ncamera perspective 1 10 0.1 100 0 true environment 0 0 0 1 1\nend\n";
    REQUIRE_THROWS_AS(ParseScene(duplicatePrimary, assets), SceneFormatError);
}

TEST_CASE("Rendering components expose reflection metadata",
    "[KairoEngineCore][Rendering][Reflection]")
{
    kairo::reflection::ReflectionRegistry registry;
    kairo::engine::RegisterEngineCoreReflection(registry);
    CHECK(registry.Contains("Kairo.Engine.CameraComponent"));
    CHECK(registry.Contains("Kairo.Engine.LightComponent"));
    CHECK(registry.Contains("Kairo.Engine.EnvironmentComponent"));
    CHECK(registry.Require("Kairo.Engine.MeshRendererComponent").Properties.size() == 6u);
    CHECK(registry.Require("Kairo.Engine.LightComponent").Properties.size() == 11u);
}
