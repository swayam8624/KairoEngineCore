#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

import Kairo.EngineCore;
import Kairo.Reflection;

using namespace kairo::engine;
using namespace kairo::reflection;
using namespace kairo::foundation::math;

namespace
{
    [[nodiscard]] ReflectionRegistry EngineRegistry()
    {
        ReflectionRegistry registry;
        RegisterEngineCoreReflection(registry);
        return registry;
    }

    [[nodiscard]] kairo::assets::AssetID Asset(std::string_view text)
    {
        return kairo::assets::AssetID::Parse(text);
    }
}

TEST_CASE("Transform participates in generic reflected scene traversal",
    "[EngineCore][ReflectionV2][Transform]")
{
    Scene scene;
    const Entity entity = scene.CreateEntity("Reflected");

    const auto components = EnumerateReflectedComponents(scene, entity);
    const auto found = std::find_if(components.begin(), components.end(),
        [](const ReflectedSceneComponent& component)
        {
            return component.TypeKey == "Kairo.Engine.TransformComponent";
        });

    REQUIRE(found != components.end());
    CHECK(found->Object == &scene.Transform(entity));
    CHECK(ResolveReflectedComponent(scene, entity, "Kairo.Engine.TransformComponent") ==
        &scene.Transform(entity));
}

TEST_CASE("Reflection V2 reads and writes EngineCore TRS composites",
    "[EngineCore][ReflectionV2][Transform]")
{
    ReflectionRegistry registry = EngineRegistry();
    TransformComponent transform;

    CHECK(registry.Read("Kairo.Engine.TransformComponent", "translation", &transform) ==
        PropertyValue(Vector3Value{ 0.0, 0.0, 0.0 }));
    CHECK(registry.Read("Kairo.Engine.TransformComponent", "rotation", &transform) ==
        PropertyValue(QuaternionValue{ 0.0, 0.0, 0.0, 1.0 }));
    CHECK(registry.Read("Kairo.Engine.TransformComponent", "scale", &transform) ==
        PropertyValue(Vector3Value{ 1.0, 1.0, 1.0 }));

    registry.Write("Kairo.Engine.TransformComponent", "translation", &transform,
        PropertyValue(Vector3Value{ 1.25, -2.5, 9.0 }));
    registry.Write("Kairo.Engine.TransformComponent", "scale", &transform,
        PropertyValue(Vector3Value{ 2.0, 3.0, 4.0 }));
    registry.Write("Kairo.Engine.TransformComponent", "rotation", &transform,
        PropertyValue(QuaternionValue{ 0.0, 0.0, 0.0, 2.0 }));
    ValidateReflectedComponent("Kairo.Engine.TransformComponent", &transform);

    CHECK(transform.Local.Translation == Vec3f{ 1.25f, -2.5f, 9.0f });
    CHECK(transform.Local.Scale == Vec3f{ 2.0f, 3.0f, 4.0f });
    CHECK(transform.Local.Rotation.x == Catch::Approx(0.0f));
    CHECK(transform.Local.Rotation.y == Catch::Approx(0.0f));
    CHECK(transform.Local.Rotation.z == Catch::Approx(0.0f));
    CHECK(transform.Local.Rotation.w == Catch::Approx(1.0f));

    const Vec3f before = transform.Local.Translation;
    REQUIRE_THROWS_AS(
        registry.Write("Kairo.Engine.TransformComponent", "translation", &transform,
            PropertyValue(Vector3Value{
                static_cast<double>(std::numeric_limits<float>::max()) * 4.0,
                0.0,
                0.0 })),
        std::out_of_range);
    CHECK(transform.Local.Translation == before);

    REQUIRE_THROWS_AS(
        registry.Write("Kairo.Engine.TransformComponent", "rotation", &transform,
            PropertyValue(QuaternionValue{ 0.0, 0.0, 0.0, 0.0 })),
        std::invalid_argument);
    CHECK(transform.Local.Rotation.w == Catch::Approx(1.0f));
}

TEST_CASE("EngineCore enums use canonical Reflection V2 options",
    "[EngineCore][ReflectionV2][Enum]")
{
    ReflectionRegistry registry = EngineRegistry();
    CameraComponent camera;

    CHECK(registry.Read("Kairo.Engine.CameraComponent", "projection", &camera) ==
        PropertyValue(EnumerationValue{
            static_cast<std::int64_t>(CameraProjection::Perspective),
            "Kairo.Engine.CameraProjection.Perspective" }));

    registry.Write("Kairo.Engine.CameraComponent", "projection", &camera,
        PropertyValue(EnumerationValue{
            static_cast<std::int64_t>(CameraProjection::Orthographic),
            "Kairo.Engine.CameraProjection.Orthographic" }));
    CHECK(camera.Projection == CameraProjection::Orthographic);
    ValidateReflectedComponent("Kairo.Engine.CameraComponent", &camera);

    REQUIRE_THROWS_AS(
        registry.Write("Kairo.Engine.CameraComponent", "projection", &camera,
            PropertyValue(EnumerationValue{ 99, "Kairo.Engine.CameraProjection.Unknown" })),
        std::out_of_range);
    CHECK(camera.Projection == CameraProjection::Orthographic);
}

TEST_CASE("Renderer colors and collider extents use composite reflection values",
    "[EngineCore][ReflectionV2][Composite]")
{
    ReflectionRegistry registry = EngineRegistry();

    LightComponent light;
    registry.Write("Kairo.Engine.LightComponent", "color", &light,
        PropertyValue(Vector3Value{ 0.25, 0.5, 0.75 }));
    ValidateReflectedComponent("Kairo.Engine.LightComponent", &light);
    CHECK(light.Color == Vec3f{ 0.25f, 0.5f, 0.75f });

    ColliderComponent collider;
    registry.Write("Kairo.Engine.ColliderComponent", "half-extents", &collider,
        PropertyValue(Vector3Value{ 2.0, 1.0, 0.5 }));
    ValidateReflectedComponent("Kairo.Engine.ColliderComponent", &collider);
    CHECK(collider.HalfExtents == Vec3f{ 2.0f, 1.0f, 0.5f });
}

TEST_CASE("Asset handles cross reflection as stable typed UUID references",
    "[EngineCore][ReflectionV2][Reference]")
{
    ReflectionRegistry registry = EngineRegistry();

    MeshRendererComponent renderer;
    renderer.MeshAsset.ID = Asset("11111111-1111-4111-8111-111111111111");
    renderer.MaterialAsset.ID = Asset("22222222-2222-4222-8222-222222222222");
    renderer.Validate();

    CHECK(registry.Read("Kairo.Engine.MeshRendererComponent", "mesh-asset", &renderer) ==
        PropertyValue(ReferenceValue{
            "Kairo.Assets.Mesh",
            "11111111-1111-4111-8111-111111111111" }));

    registry.Write("Kairo.Engine.MeshRendererComponent", "mesh-asset", &renderer,
        PropertyValue(ReferenceValue{
            "Kairo.Assets.Mesh",
            "33333333-3333-4333-8333-333333333333" }));
    ValidateReflectedComponent("Kairo.Engine.MeshRendererComponent", &renderer);
    CHECK(renderer.MeshAsset.ID.ToString() == "33333333-3333-4333-8333-333333333333");

    LogicComponent logic;
    logic.Document.ID = Asset("44444444-4444-4444-8444-444444444444");
    logic.Validate();
    CHECK(registry.Read("Kairo.Engine.LogicComponent", "document", &logic) ==
        PropertyValue(ReferenceValue{
            "Kairo.Assets.Document",
            "44444444-4444-4444-8444-444444444444" }));

    REQUIRE_THROWS_AS(
        registry.Write("Kairo.Engine.MeshRendererComponent", "mesh-asset", &renderer,
            PropertyValue(ReferenceValue{
                "Kairo.Assets.Material",
                "55555555-5555-4555-8555-555555555555" })),
        std::invalid_argument);
}
