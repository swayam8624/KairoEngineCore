#include <catch2/catch_test_macros.hpp>
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
