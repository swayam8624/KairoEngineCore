#include <catch2/catch_test_macros.hpp>
#include <string>

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.LogicState;
import Kairo.Foundation.Math.Vector;

TEST_CASE("Logic state persists typed values across snapshots")
{
    using namespace kairo::engine;
    LogicState state;
    state.Set("alive", true);
    state.Set("score", 12.5);
    state.Set("spawn", kairo::foundation::math::Vec3d{ 1.0, 2.0, 3.0 });
    state.Set("target", Entity{ 42u });
    state.Set("label", std::string("Player"));

    CHECK(state.Get<bool>("alive"));
    CHECK(state.Get<double>("score") == 12.5);
    CHECK(state.Get<Entity>("target") == Entity{ 42u });

    LogicState restored;
    restored.Restore(state.Snapshot());
    CHECK(restored.Snapshot() == state.Snapshot());
    CHECK(restored.Get<std::string>("label") == "Player");
}

TEST_CASE("Logic timers expire deterministically and preserve repeat overshoot")
{
    using namespace kairo::engine;
    LogicState state;
    state.StartTimer("zeta", 0.25);
    state.StartTimer("alpha", 0.5, 0.5);

    CHECK(state.TickTimers(0.2).empty());
    const auto first = state.TickTimers(0.3);
    REQUIRE(first.size() == 2u);
    CHECK(first[0] == "alpha");
    CHECK(first[1] == "zeta");
    CHECK_FALSE(state.HasTimer("zeta"));
    CHECK(state.HasTimer("alpha"));

    CHECK(state.TickTimers(0.49).empty());
    const auto repeat = state.TickTimers(0.01);
    REQUIRE(repeat.size() == 1u);
    CHECK(repeat[0] == "alpha");
}

TEST_CASE("Logic state rejects invalid runtime values")
{
    using namespace kairo::engine;
    LogicState state;
    CHECK_THROWS(state.Set("invalid_entity", Entity{}));
    CHECK_THROWS(state.StartTimer("negative", -1.0));
    CHECK_THROWS(state.TickTimers(-0.1));
    state.Set("value", 1.0);
    CHECK_THROWS(state.Get<bool>("value"));
}
