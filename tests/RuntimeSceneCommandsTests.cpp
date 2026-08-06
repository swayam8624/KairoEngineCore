#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.RuntimeSceneCommands;
import Kairo.EngineCore.Scene;
import Kairo.Foundation.Math;

TEST_CASE("Runtime scene commands resolve pending entities in FIFO order")
{
    using namespace kairo::engine;
    Scene scene;
    RuntimeSceneCommandBuffer commands;
    const PendingEntity parent = commands.Create("Parent");
    const PendingEntity child = commands.Create("Child", RuntimeEntityReference{ parent });
    commands.SetPosition(child, { 1.0f, 2.0f, 3.0f });
    commands.AddTag(child, "collectible");
    commands.SetEnabled(parent, false);

    const RuntimeSceneCommit commit = commands.Commit(scene);
    const Entity parentEntity = commit.Resolve(parent);
    const Entity childEntity = commit.Resolve(child);

    CHECK(commit.AppliedCommands == 5u);
    CHECK(scene.Parent(childEntity) == parentEntity);
    CHECK(scene.Transform(childEntity).Local.Translation.x == 1.0f);
    CHECK(scene.Transform(childEntity).Local.Translation.y == 2.0f);
    CHECK(scene.Transform(childEntity).Local.Translation.z == 3.0f);
    CHECK(scene.HasTag(childEntity, "collectible"));
    CHECK_FALSE(scene.IsActiveInHierarchy(childEntity));
    CHECK(commands.Empty());
}

TEST_CASE("Runtime scene tag queries are deterministic")
{
    using namespace kairo::engine;
    Scene scene;
    const Entity first = scene.CreateEntity("First");
    const Entity second = scene.CreateEntity("Second");
    scene.AddTag(second, "enemy");
    scene.AddTag(first, "enemy");

    const auto matches = FindEntitiesWithTag(scene, "enemy");
    REQUIRE(matches.size() == 2u);
    CHECK(matches[0] == first);
    CHECK(matches[1] == second);
    REQUIRE(FindFirstEntityWithTag(scene, "enemy").has_value());
    CHECK(*FindFirstEntityWithTag(scene, "enemy") == first);
}

TEST_CASE("Runtime scene commands reject unresolved pending references")
{
    using namespace kairo::engine;
    Scene scene;
    RuntimeSceneCommandBuffer commands;
    commands.SetEnabled(PendingEntity{ 99u }, false);
    CHECK_THROWS(commands.Commit(scene));
}
