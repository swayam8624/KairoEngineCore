#include <cmath>

#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.ProductionSystems;

TEST_CASE("Animation clips sample deterministic channels")
{
    using namespace kairo::engine;
    AnimationClip clip(2.0);
    clip.AddKey("root", { 0.0, {0.0,0.0,0.0} });
    clip.AddKey("root", { 2.0, {2.0,4.0,6.0} });
    const auto value = clip.Sample("root", 1.0, false);
    CHECK(value.X == 1.0);
    CHECK(value.Y == 2.0);
    CHECK(value.Z == 3.0);
}

TEST_CASE("Terrain sculpt and foliage scattering are deterministic")
{
    using namespace kairo::engine;
    TerrainHeightfield terrain(8u, 8u, 2.0);
    terrain.Sculpt(6.0, 6.0, 5.0, 3.0);
    CHECK(terrain.HeightAt(3u,3u) > 0.0);
    const auto first = ScatterFoliage(terrain, 16u, 42u);
    const auto second = ScatterFoliage(terrain, 16u, 42u);
    REQUIRE(first.size() == second.size());
    CHECK(first.front().Position.X == second.front().Position.X);
    CHECK(first.front().RotationY == second.front().RotationY);
}

TEST_CASE("Particle cloth fluid and streaming systems advance bounded state")
{
    using namespace kairo::engine;
    ParticleEmitter emitter(4u);
    emitter.Emit({ {}, {1.0,0.0,0.0}, 1.0 });
    emitter.Update(0.5);
    REQUIRE(emitter.Particles().size() == 1u);
    CHECK(emitter.Particles().front().Position.X == 0.5);
    emitter.Update(0.6);
    CHECK(emitter.Particles().empty());

    ClothSimulation cloth;
    const auto a = cloth.AddParticle({ {0.0,0.0,0.0}, {0.0,0.0,0.0}, true });
    const auto b = cloth.AddParticle({ {1.0,0.0,0.0}, {1.0,0.0,0.0}, false });
    cloth.AddConstraint(a,b);
    cloth.Step(1.0/60.0);
    CHECK(cloth.Particles().front().Position.Y == 0.0);
    CHECK(cloth.Particles()[1].Position.Y < 0.0);

    FluidGrid fluid(5u,5u);
    fluid.AddDensity(2u,2u,1.0);
    fluid.Diffuse(0.2);
    CHECK(fluid.Density(2u,2u) < 1.0);
    CHECK(fluid.Density(1u,2u) > 0.0);

    WorldStreamer streamer(100.0,1);
    streamer.Update(0.0,0.0);
    CHECK(streamer.Loaded().size() == 9u);
    streamer.Update(250.0,0.0);
    CHECK(streamer.Loaded().contains({2,0}));
}
