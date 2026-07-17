#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
import Kairo.EngineCore;
import Kairo.Foundation.Math.Vector;
import Kairo.Reflection;
using namespace kairo::engine;

namespace
{
    const auto MeshAsset = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000101");
    const auto MaterialAsset = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000102");
    const auto LogicAsset = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000103");
}

TEST_CASE("Scene owns stable entity records", "[KairoEngineCore][Scene]")
{
    Scene scene;
    const Entity first = scene.CreateEntity("Cube");
    const Entity second = scene.CreateEntity("Floor");
    REQUIRE(first.Value == 1u);
    REQUIRE(second.Value == 2u);
    scene.Transform(first).Local.Translation.x = 2.0f;
    CHECK(scene.Transform(first).Local.Translation.x == 2.0f);
    scene.DestroyEntity(first);
    CHECK_FALSE(scene.Contains(first));
    CHECK(scene.Contains(second));
    const Scene& readOnly = scene;
    const auto entities = readOnly.Entities();
    REQUIRE(entities.size() == 1u);
    CHECK(entities.front() == second);
    CHECK(readOnly.Name(second).Value == "Floor");
}

TEST_CASE("EngineCore owns versioned project descriptors for tools and players",
    "[KairoEngineCore][Project]")
{
    ProjectDescriptor descriptor{ "Runtime Project", "Assets.kassets", "Scenes/Main.kscene" };
    descriptor.EnabledPlugins = { "kairo.gameplay" };
    descriptor.BuildProfiles = {
        { "Shipping", ProjectBuildKind::Release, "Artifacts/Shipping" } };
    const std::string encoded = SerializeProjectDescriptor(descriptor);
    CHECK(encoded.starts_with("kairo-project 2\n"));
    CHECK(ParseProjectDescriptor(encoded) == descriptor);

    const auto migrated = ParseProjectDescriptor(
        "kairo-project 1\nname \"Legacy\"\nassets \"Assets.kassets\"\n"
        "startup-scene \"Scenes/Main.kscene\"\n");
    CHECK(migrated.EngineVersion == "0.1.0");
    CHECK(migrated.BuildProfiles.size() == 2u);

    const auto root = std::filesystem::temp_directory_path() /
        ("kairo-core-project-" + kairo::assets::GenerateAssetID().ToString());
    const auto path = root / "Game.kproject";
    SaveProjectDescriptor(path, descriptor);
    CHECK(LoadProjectDescriptor(path) == descriptor);
    CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    std::filesystem::remove_all(root);
}

TEST_CASE("Scene restores explicit IDs without corrupting automatic allocation", "[KairoEngineCore][Scene][Serialization]")
{
    Scene scene;
    const Entity restored = scene.CreateEntityWithID({ 41u }, "Restored");
    CHECK(restored.Value == 41u);
    CHECK(scene.CreateEntity("Next").Value == 42u);
    REQUIRE_THROWS_AS(scene.CreateEntityWithID({}, "Invalid"), std::invalid_argument);
    REQUIRE_THROWS_AS(scene.CreateEntityWithID(restored, "Duplicate"), std::invalid_argument);

    Scene exhausted;
    const Entity last = exhausted.CreateEntityWithID(
        { std::numeric_limits<std::uint32_t>::max() }, "Last");
    CHECK(last.Value == std::numeric_limits<std::uint32_t>::max());
    REQUIRE_THROWS_AS(exhausted.CreateEntity(), std::overflow_error);
}

TEST_CASE("Scene hierarchy enforces ownership cycles and deterministic metadata",
    "[KairoEngineCore][Scene][Hierarchy]")
{
    Scene scene;
    const Entity root = scene.CreateEntity("Root");
    const Entity child = scene.CreateEntity("Child");
    const Entity grandchild = scene.CreateEntity("Grandchild");
    const Entity alternate = scene.CreateEntity("Alternate");
    scene.SetParent(child, root);
    scene.SetParent(grandchild, child);
    scene.Transform(root).Local.Translation = { 10.0f, 0.0f, 0.0f };
    scene.Transform(child).Local.Translation = { 0.0f, 2.0f, 0.0f };
    scene.Transform(grandchild).Local.Translation = { 0.0f, 0.0f, 3.0f };
    CHECK(scene.Parent(child) == root);
    CHECK(scene.Children(root) == std::vector<Entity>{ child });
    CHECK(scene.RootEntities() == std::vector<Entity>{ root, alternate });
    CHECK(scene.WorldTransform(grandchild).Translation ==
        kairo::foundation::math::Vec3f{ 10.0f, 2.0f, 3.0f });
    REQUIRE_THROWS_AS(scene.SetParent(root, grandchild), std::invalid_argument);
    CHECK_FALSE(scene.Parent(root).has_value());
    CHECK(scene.Parent(grandchild) == child);

    scene.SetParent(child, alternate);
    CHECK(scene.Children(root).empty());
    CHECK(scene.Children(alternate) == std::vector<Entity>{ child });
    scene.SetEnabled(child, false);
    scene.SetLayer(child, 7u);
    scene.AddTag(child, "player");
    scene.AddTag(child, "actor");
    scene.AddTag(child, "player");
    CHECK_FALSE(scene.IsEnabled(child));
    CHECK_FALSE(scene.IsActiveInHierarchy(grandchild));
    CHECK(scene.Layer(child) == 7u);
    CHECK(scene.Tags(child) == std::vector<std::string>{ "actor", "player" });
    CHECK(scene.HasTag(child, "player"));
    CHECK(scene.RemoveTag(child, "player"));
    CHECK_FALSE(scene.RemoveTag(child, "player"));
    REQUIRE_THROWS_AS(scene.SetLayer(child, MaximumSceneLayer + 1u), std::invalid_argument);

    scene.DestroyEntity(alternate);
    CHECK(scene.Contains(root));
    CHECK_FALSE(scene.Contains(alternate));
    CHECK_FALSE(scene.Contains(child));
    CHECK_FALSE(scene.Contains(grandchild));
}

TEST_CASE("Scene V2 migrates V1 defaults and resolves forward hierarchy references",
    "[KairoEngineCore][Scene][Serialization][V2]")
{
    kairo::assets::AssetRegistry assets;
    const std::string versionOne =
        "kairo-scene 1\nentity 5 \"Legacy\"\ntransform 0 0 0 0 0 0 1 1 1 1\n"
        "rigid-body 17\ncollider 23\nend\n";
    const Scene legacy = ParseScene(versionOne, assets);
    CHECK(legacy.IsEnabled({ 5u }));
    CHECK(legacy.Layer({ 5u }) == 0u);
    CHECK(legacy.Tags({ 5u }).empty());
    CHECK_FALSE(legacy.Parent({ 5u }).has_value());
    CHECK(legacy.RigidBody({ 5u }).Motion == RigidBodyMotion::Dynamic);
    CHECK(legacy.Collider({ 5u }).Shape == ColliderShape::Box);
    CHECK(SerializeScene(legacy, assets).starts_with("kairo-scene 2\n"));

    const std::string versionTwo =
        "kairo-scene 2\n"
        "entity 2 \"Child\"\nparent 1\nenabled false\nlayer 3\ntag \"player\"\n"
        "transform 0 1 0 0 0 0 1 1 1 1\nend\n"
        "entity 1 \"Root\"\nenabled true\nlayer 0\n"
        "transform 0 0 0 0 0 0 1 1 1 1\nend\n";
    const Scene restored = ParseScene(versionTwo, assets);
    CHECK(restored.Parent({ 2u }) == Entity{ 1u });
    CHECK(restored.Children({ 1u }) == std::vector<Entity>{ Entity{ 2u } });
    CHECK_FALSE(restored.IsEnabled({ 2u }));
    CHECK(restored.Layer({ 2u }) == 3u);
    CHECK(restored.HasTag({ 2u }, "player"));
    CHECK(ParseScene(SerializeScene(restored, assets), assets).Parent({ 2u }) == Entity{ 1u });

    REQUIRE_THROWS_AS(ParseScene(
        "kairo-scene 2\nentity 1 \"Orphan\"\nparent 99\nend\n", assets), SceneFormatError);
    REQUIRE_THROWS_AS(ParseScene(
        "kairo-scene 2\nentity 1 \"A\"\nparent 2\nend\n"
        "entity 2 \"B\"\nparent 1\nend\n", assets), SceneFormatError);
}

TEST_CASE("Scene serialization round trips authored components and persistent assets", "[KairoEngineCore][Scene][Serialization]")
{
    kairo::assets::AssetRegistry assets;
    assets.Insert({ MeshAsset, kairo::assets::AssetType::Mesh, kairo::assets::AssetOrigin::SourceFile,
        "meshes/cube.obj", "kairo.obj", 1u, {} });
    assets.Insert({ MaterialAsset, kairo::assets::AssetType::Material, kairo::assets::AssetOrigin::Generated,
        "materials/default.kmat", "kairo.material", 1u, {} });
    assets.Insert({ LogicAsset, kairo::assets::AssetType::Document, kairo::assets::AssetOrigin::SourceFile,
        "logic/player.kdoc", "kairo.document-v1", 1u, {} });

    Scene original;
    const Entity cube = original.CreateEntityWithID({ 9u }, "Cube \"Hero\"");
    original.Transform(cube).Local.Translation = { 1.25f, -2.5f, 3.75f };
    original.Transform(cube).Local.Scale = { 2.0f, 0.5f, 4.0f };
    original.SetMeshRenderer(cube, { { MeshAsset }, { MaterialAsset }, false });
    const Entity camera = original.CreateEntityWithID({ 42u }, "Main Camera");
    original.SetCamera(camera, { 0.9f, 0.2f, 500.0f, true });
    original.SetLogic(cube, { { LogicAsset }, true });
    original.SetRigidBody(cube, { RigidBodyMotion::Kinematic, 2.5f, 0.25f, 0.1f, 0.2f });
    original.SetCollider(cube, {
        .Shape = ColliderShape::Capsule,
        .Radius = 0.75f,
        .HalfHeight = 1.25f,
        .Friction = 0.8f,
        .Restitution = 0.35f,
        .BelongsTo = 8u,
        .CollidesWith = 3u,
        .IsTrigger = true });
    original.SetParent(cube, camera);
    original.SetEnabled(cube, false);
    original.SetLayer(cube, 4u);
    original.AddTag(cube, "hero");

    const std::string encoded = SerializeScene(original, assets);
    CHECK(encoded.find("rigid-body kinematic ") != std::string::npos);
    CHECK(encoded.find("collider capsule ") != std::string::npos);
    Scene restored = ParseScene(encoded, assets);
    REQUIRE(restored.Entities() == std::vector<Entity>{ cube, camera });
    CHECK(restored.Name(cube).Value == "Cube \"Hero\"");
    CHECK(restored.Transform(cube).Local == original.Transform(cube).Local);
    CHECK(restored.MeshRenderer(cube).MeshAsset.ID == MeshAsset);
    CHECK_FALSE(restored.MeshRenderer(cube).Visible);
    CHECK(restored.Camera(camera).Primary);
    CHECK(restored.Logic(cube).Document.ID == LogicAsset);
    CHECK(restored.Logic(cube).Enabled);
    CHECK(restored.RigidBody(cube).Motion == RigidBodyMotion::Kinematic);
    CHECK(restored.RigidBody(cube).Density == 2.5f);
    CHECK(restored.Collider(cube).Shape == ColliderShape::Capsule);
    CHECK(restored.Collider(cube).Radius == 0.75f);
    CHECK(restored.Collider(cube).HalfHeight == 1.25f);
    CHECK(restored.Collider(cube).BelongsTo == 8u);
    CHECK(restored.Collider(cube).CollidesWith == 3u);
    CHECK(restored.Collider(cube).IsTrigger);
    CHECK(restored.Parent(cube) == camera);
    CHECK_FALSE(restored.IsEnabled(cube));
    CHECK(restored.Layer(cube) == 4u);
    CHECK(restored.HasTag(cube, "hero"));
    CHECK(restored.CreateEntity("After restore").Value == 43u);
}

TEST_CASE("Scene parser reports source locations and validates registry references", "[KairoEngineCore][Scene][Serialization]")
{
    kairo::assets::AssetRegistry assets;
    const std::string malformed = "kairo-scene 1\nentity 1 \"Broken\"\ntransform 0 0 nope 0 0 0 1 1 1 1\nend\n";
    try
    {
        (void)ParseScene(malformed, assets);
        FAIL("Expected a located parse error");
    }
    catch (const SceneFormatError& error)
    {
        CHECK(error.Line == 3u);
        CHECK(error.Column == 15u);
    }

    const std::string missingAsset =
        "kairo-scene 1\nentity 1 \"Cube\"\nmesh-renderer " + MeshAsset.ToString() + " " +
        MaterialAsset.ToString() + " true\nend\n";
    REQUIRE_THROWS_AS(ParseScene(missingAsset, assets), SceneFormatError);
    const std::string missingLogic =
        "kairo-scene 2\nentity 1 \"Actor\"\nlogic " + LogicAsset.ToString() + " true\nend\n";
    REQUIRE_THROWS_AS(ParseScene(missingLogic, assets), SceneFormatError);
}

TEST_CASE("Scene files replace destinations atomically after complete validation", "[KairoEngineCore][Scene][Serialization]")
{
    kairo::assets::AssetRegistry assets;
    Scene scene;
    const Entity entity = scene.CreateEntityWithID({ 5u }, "Saved");
    const auto path = std::filesystem::temp_directory_path() /
        ("kairo-scene-test-" + kairo::assets::GenerateAssetID().ToString() + ".kscene");
    SaveScene(path, scene, assets);
    Scene loaded;
    LoadScene(path, assets, loaded);
    CHECK(loaded.Contains(entity));
    CHECK(loaded.Name(entity).Value == "Saved");
    std::filesystem::remove(path);
}

namespace
{
    class RecordingLayer final : public Layer
    {
    public:
        explicit RecordingLayer(int& updates, bool handles) : Layer("Recording"), m_Updates(updates), m_Handles(handles) {}
        void OnUpdate(float) override { ++m_Updates; }
        void OnEvent(Event& event) override { if (m_Handles) event.Handled = true; }
    private:
        int& m_Updates;
        bool m_Handles;
    };

    class InputObservingLayer final : public Layer
    {
    public:
        InputObservingLayer(const InputState& input, bool& sawPress)
            : Layer("Input observer"), m_Input(input), m_SawPress(sawPress) {}
        void OnUpdate(float) override
        {
            m_SawPress = m_Input.IsKeyPressed(static_cast<std::int32_t>(KeyCode::Space));
        }
    private:
        const InputState& m_Input;
        bool& m_SawPress;
    };
}

TEST_CASE("Application updates layers and propagates events in reverse order", "[KairoEngineCore][Application]")
{
    int updates = 0;
    Application app;
    app.PushLayer(std::make_unique<RecordingLayer>(updates, false));
    app.PushLayer(std::make_unique<RecordingLayer>(updates, true));
    app.RunFrame();
    CHECK(updates == 2);
    Event event{ EventType::KeyPressed };
    app.Dispatch(event);
    CHECK(event.Handled);
}

TEST_CASE("Engine events retain typed payload and handled state", "[KairoEngineCore][Event]")
{
    Event resize{ EventType::WindowResize, false, 1920, 1080 };
    CHECK(resize.Name() == "WindowResize");
    CHECK_FALSE(resize.Handled);
    resize.Handled = true;
    CHECK(resize.Handled);
}

TEST_CASE("Application owns platform-neutral held and frame-scoped input", "[KairoEngineCore][Input]")
{
    Application app;
    bool layerSawPress = false;
    app.PushLayer(std::make_unique<InputObservingLayer>(app.Input(), layerSawPress));
    Event press{ EventType::KeyPressed, false, static_cast<std::int32_t>(KeyCode::Space) };
    app.Dispatch(press);
    CHECK(app.Input().IsKeyDown(KeyCode::Space));
    Event mouse{ EventType::MouseMoved, false, 40, 20 };
    Event scroll{ EventType::MouseScrolled, false, 0, 3 };
    app.Dispatch(mouse);
    app.Dispatch(scroll);
    CHECK(app.Input().MousePosition().X == 40.0f);
    CHECK(app.Input().ScrollDelta().Y == 3.0f);
    app.RunFrame();
    CHECK(layerSawPress);
    CHECK(app.Input().MouseDelta().X == 0.0f);
    CHECK(app.Input().ScrollDelta().Y == 0.0f);
    Event release{ EventType::KeyReleased, false, static_cast<std::int32_t>(KeyCode::Space) };
    app.Dispatch(release);
    CHECK_FALSE(app.Input().IsKeyDown(KeyCode::Space));
}

TEST_CASE("Versioned input maps evaluate keyboard mouse and gamepad actions",
    "[KairoEngineCore][Input][ActionMap]")
{
    const std::string source =
        "kairo-input 1\n"
        "action \"Move\" axis2d\n"
        "action \"Jump\" button\n"
        "action \"Look\" axis2d\n"
        "bind \"Move\" key W 0 1 0\n"
        "bind \"Move\" key S 0 -1 0\n"
        "bind \"Move\" key A -1 0 0\n"
        "bind \"Move\" key D 1 0 0\n"
        "bind \"Jump\" key Space 1 0 0\n"
        "bind \"Jump\" gamepad-button A 1 0 0\n"
        "bind \"Look\" gamepad-axis RightX 1 0 0.2\n"
        "bind \"Look\" gamepad-axis RightY 0 -1 0.2\n";
    const InputActionMap map = ParseInputActionMap(source);
    CHECK(ParseInputActionMap(SerializeInputActionMap(map)) == map);
    const auto inputPath = std::filesystem::temp_directory_path() /
        ("kairo-input-" + kairo::assets::GenerateAssetID().ToString() + ".kinput");
    SaveInputActionMap(inputPath, map);
    CHECK(LoadInputActionMap(inputPath) == map);
    std::filesystem::remove(inputPath);

    InputState input;
    input.BeginFrame();
    input.SetKeyDown(KeyCode::W, true);
    const auto started = map.Evaluate("Move", input);
    CHECK(started.Down);
    CHECK(started.Pressed);
    CHECK_FALSE(started.Released);
    CHECK(started.Value.Y == 1.0f);

    input.BeginFrame();
    const auto held = map.Evaluate("Move", input);
    CHECK(held.Down);
    CHECK_FALSE(held.Pressed);
    input.SetKeyDown(KeyCode::W, false);
    const auto released = map.Evaluate("Move", input);
    CHECK_FALSE(released.Down);
    CHECK(released.Released);

    input.BeginFrame();
    input.SetGamepadAxis(0u, GamepadAxis::RightX, 0.6f);
    input.SetGamepadAxis(0u, GamepadAxis::RightY, -0.6f);
    const auto look = map.Evaluate("Look", input);
    CHECK(std::abs(look.Value.X - 0.5f) < 1.0e-5f);
    CHECK(std::abs(look.Value.Y - 0.5f) < 1.0e-5f);
    input.SetGamepadButtonDown(0u, GamepadButton::A, true);
    CHECK(map.Evaluate("Jump", input).Pressed);

    input.BeginFrame();
    input.SetMouseButtonDown(MouseButton::Left, true);
    CHECK(input.IsMouseButtonPressed(MouseButton::Left));
    input.SetMousePosition({ 10.5f, 20.25f });
    CHECK(input.MouseDelta().X == 10.5f);
    CHECK(input.MouseDelta().Y == 20.25f);
    input.AddScrollDelta({ 0.0f, -0.5f });
    CHECK(input.ScrollDelta().Y == -0.5f);
    CHECK_THROWS(input.SetMousePosition({ std::numeric_limits<float>::infinity(), 0.0f }));
    CHECK_THROWS(input.SetGamepadAxis(InputState::MaximumGamepads, 0u, 0.0f));

    CHECK(ParseInputActionMap("kairo-input 1\n").Actions().empty());

    try
    {
        (void)ParseInputActionMap(
            "kairo-input 1\naction \"Jump\" button\nbind \"Jump\" key NotAKey 1 0 0\n");
        FAIL("Unknown key was accepted.");
    }
    catch (const InputMapFormatError& error)
    {
        CHECK(error.Line == 3u);
        CHECK(error.Column == 17u);
    }
    CHECK_THROWS_AS(ParseInputActionMap(
        "kairo-input 1\naction \"Unused\" button\n"), InputMapFormatError);
}

TEST_CASE("Runtime components reject invalid public configuration", "[KairoEngineCore][Components]")
{
    MeshRendererComponent mesh{ { MeshAsset }, { MaterialAsset }, true };
    REQUIRE_NOTHROW(mesh.Validate());
    mesh.MaterialAsset = {};
    REQUIRE_THROWS(mesh.Validate());
    CameraComponent camera;
    REQUIRE_NOTHROW(camera.Validate());
    camera.FarPlane = camera.NearPlane;
    REQUIRE_THROWS(camera.Validate());
}

TEST_CASE("Scene owns optional authored components and stable render extraction", "[KairoEngineCore][Scene][Components]")
{
    Scene scene;
    const Entity hidden = scene.CreateEntity("Hidden");
    const Entity visible = scene.CreateEntity("Visible");
    const Entity camera = scene.CreateEntity("Camera");

    scene.SetMeshRenderer(hidden, { { MeshAsset }, { MaterialAsset }, false });
    scene.SetMeshRenderer(visible, { { MeshAsset }, { MaterialAsset }, true });
    scene.SetCamera(camera, CameraComponent{ .Primary = true });
    scene.SetRigidBody(visible, {});
    scene.SetCollider(visible, {});

    REQUIRE(scene.HasMeshRenderer(visible));
    CHECK(scene.MeshRenderer(visible).MeshAsset.ID == MeshAsset);
    REQUIRE(scene.HasCamera(camera));
    CHECK(scene.Camera(camera).Primary);
    CHECK(scene.RigidBody(visible).Motion == RigidBodyMotion::Dynamic);
    CHECK(scene.Collider(visible).Shape == ColliderShape::Box);
    CHECK(scene.RenderableEntities() == std::vector<Entity>{ visible });

    MeshRendererComponent invalid{ { MeshAsset }, {}, true };
    REQUIRE_THROWS_AS(scene.SetMeshRenderer(visible, invalid), std::invalid_argument);
    CHECK(scene.MeshRenderer(visible).MaterialAsset.ID == MaterialAsset);

    auto invalidBody = scene.RigidBody(visible);
    invalidBody.Density = 0.0f;
    REQUIRE_THROWS_AS(scene.SetRigidBody(visible, invalidBody), std::invalid_argument);
    CHECK(scene.RigidBody(visible).Density == 1.0f);
    auto invalidCollider = scene.Collider(visible);
    invalidCollider.Restitution = 1.5f;
    REQUIRE_THROWS_AS(scene.SetCollider(visible, invalidCollider), std::invalid_argument);
    CHECK(scene.Collider(visible).Restitution == 0.1f);

    CHECK(scene.RemoveMeshRenderer(visible));
    CHECK_FALSE(scene.RemoveMeshRenderer(visible));
    CHECK_FALSE(scene.HasMeshRenderer(visible));
    REQUIRE_THROWS_AS(scene.MeshRenderer(visible), std::logic_error);
}

TEST_CASE("Logger preserves ordering and bounded diagnostic history", "[KairoEngineCore][Logger]")
{
    Logger logger(2u);
    logger.Write(LogSeverity::Info, "Scene", "Created");
    logger.Write(LogSeverity::Warning, "Physics", "Sleeping body");
    logger.Write(LogSeverity::Error, "Renderer", "Lost surface");
    const auto records = logger.Snapshot();
    REQUIRE(records.size() == 2u);
    CHECK(records[0].Sequence == 2u);
    CHECK(records[1].Category == "Renderer");
    logger.SetMinimumSeverity(LogSeverity::Error);
    logger.Write(LogSeverity::Info, "Scene", "Ignored");
    CHECK(logger.Snapshot().size() == 2u);
}

TEST_CASE("Core diagnostics provides stable cross-system channels", "[KairoEngineCore][Diagnostics]")
{
    CoreDiagnostics diagnostics(3u);
    diagnostics.Emit(DiagnosticChannel::Renderer, LogSeverity::Info, "Frame graph compiled");
    diagnostics.Emit(DiagnosticChannel::Assets, "Import", LogSeverity::Warning, "Source texture missing mipmaps");
    diagnostics.Emit(DiagnosticChannel::Physics, {}, LogSeverity::Error, "Constraint diverged");

    const auto records = diagnostics.Snapshot();
    REQUIRE(records.size() == 3u);
    CHECK(records[0].Category == "Renderer");
    CHECK(records[1].Category == "Assets/Import");
    CHECK(records[2].Category == "Physics");
    CHECK(NameOf(DiagnosticChannel::Editor) == "Editor");

    diagnostics.SetMinimumSeverity(LogSeverity::Error);
    diagnostics.Emit(DiagnosticChannel::Tooling, LogSeverity::Info, "Ignored by filter");
    CHECK(diagnostics.Snapshot().size() == 3u);
    diagnostics.Clear();
    CHECK(diagnostics.Snapshot().empty());
}

TEST_CASE("EngineCore registers inspector-ready reflection metadata", "[KairoEngineCore][Reflection]")
{
    kairo::reflection::ReflectionRegistry registry;
    RegisterEngineCoreReflection(registry);
    REQUIRE(registry.Size() == 6u);

    CameraComponent camera;
    registry.Write("Kairo.Engine.CameraComponent", "vertical-fov-radians", &camera,
        kairo::reflection::PropertyValue(1.25));
    registry.Write("Kairo.Engine.CameraComponent", "primary", &camera,
        kairo::reflection::PropertyValue(true));
    CHECK(camera.VerticalFovRadians == 1.25f);
    CHECK(camera.Primary);
    REQUIRE_THROWS_AS(registry.Write("Kairo.Engine.CameraComponent", "near-plane", &camera,
        kairo::reflection::PropertyValue(-1.0)), std::out_of_range);

    NameComponent name{ "Original" };
    registry.Write("Kairo.Engine.NameComponent", "value", &name,
        kairo::reflection::PropertyValue("Reflected Name"));
    CHECK(name.Value == "Reflected Name");
    REQUIRE_THROWS_AS(RegisterEngineCoreReflection(registry), std::invalid_argument);
}

TEST_CASE("EngineCore resolves scene components for reflection without retaining storage pointers",
    "[KairoEngineCore][Reflection]")
{
    Scene scene;
    const Entity entity = scene.CreateEntity("Inspector Camera");
    scene.SetCamera(entity, CameraComponent{ .Primary = true });
    scene.SetLogic(entity, { { LogicAsset }, true });

    const auto components = EnumerateReflectedComponents(scene, entity);
    REQUIRE(components.size() == 3u);
    CHECK(components[0].TypeKey == "Kairo.Engine.NameComponent");
    CHECK(components[1].TypeKey == "Kairo.Engine.CameraComponent");
    CHECK(components[2].TypeKey == "Kairo.Engine.LogicComponent");
    CHECK(ResolveReflectedComponent(scene, entity, "Kairo.Engine.NameComponent") == &scene.Name(entity));
    CHECK(ResolveReflectedComponent(scene, entity, "Kairo.Engine.CameraComponent") == &scene.Camera(entity));
    CHECK(ResolveReflectedComponent(scene, entity, "Kairo.Engine.LogicComponent") == &scene.Logic(entity));
    REQUIRE_THROWS_AS(ResolveReflectedComponent(scene, entity, "Kairo.Engine.MeshRendererComponent"), std::logic_error);

    ValidateReflectedComponent("Kairo.Engine.CameraComponent", &scene.Camera(entity));
    scene.Camera(entity).FarPlane = scene.Camera(entity).NearPlane;
    REQUIRE_THROWS_AS(ValidateReflectedComponent("Kairo.Engine.CameraComponent", &scene.Camera(entity)), std::invalid_argument);
}

TEST_CASE("Job system completes queued work and propagates task results", "[KairoEngineCore][Jobs]")
{
    JobSystem jobs(2u);
    std::atomic<int> executed = 0;
    auto first = jobs.Submit([&executed] { ++executed; return 7; });
    auto second = jobs.Submit([&executed] { ++executed; return 11; });
    jobs.WaitIdle();
    CHECK(executed.load() == 2);
    const int firstResult = first.get();
    const int secondResult = second.get();
    CHECK(firstResult == 7);
    CHECK(secondResult == 11);

    // A queued task may own resources that cannot be copied. The queue stores
    // a shared envelope, so this remains valid without weakening task ownership.
    auto ownedValue = std::make_unique<int>(13);
    auto moveOnly = jobs.Submit([value = std::move(ownedValue)] { return *value; });
    jobs.WaitIdle();
    CHECK(moveOnly.get() == 13);

    // Worker exceptions belong to the future; a faulty task must not terminate
    // the worker or prevent later work from completing.
    auto failing = jobs.Submit([]() -> int { throw std::runtime_error("expected task failure"); });
    REQUIRE_THROWS_AS(failing.get(), std::runtime_error);
    REQUIRE_THROWS(JobSystem(257u));
}
