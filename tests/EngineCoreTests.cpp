#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <memory>
import Kairo.EngineCore;
using namespace kairo::engine;
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
    CHECK(app.Input().MouseDelta().X == 0.0f);
    CHECK(app.Input().ScrollDelta().Y == 0.0f);
    Event release{ EventType::KeyReleased, false, static_cast<std::int32_t>(KeyCode::Space) };
    app.Dispatch(release);
    CHECK_FALSE(app.Input().IsKeyDown(KeyCode::Space));
}

TEST_CASE("Runtime components reject invalid public configuration", "[KairoEngineCore][Components]")
{
    MeshRendererComponent mesh{ "mesh/cube", "material/default", true };
    REQUIRE_NOTHROW(mesh.Validate());
    mesh.MaterialAsset.clear();
    REQUIRE_THROWS(mesh.Validate());
    CameraComponent camera;
    REQUIRE_NOTHROW(camera.Validate());
    camera.FarPlane = camera.NearPlane;
    REQUIRE_THROWS(camera.Validate());
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
    REQUIRE_THROWS(JobSystem(257u));
}
