#include <catch2/catch_test_macros.hpp>
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
