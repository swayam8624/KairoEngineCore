#include <cmath>
#include <limits>
#include <stdexcept>

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

TEST_CASE("particle bursts are deterministic, bounded and leave state unchanged on rejection")
{
    using namespace kairo::engine;
    ParticleEmitter a(128),b(128);
    ParticleBurst burst;burst.Origin={1,2,3};burst.BaseVelocity={0,2,0};
    burst.Count=128;burst.Seed=1923;
    a.EmitBurst(burst);b.EmitBurst(burst);
    REQUIRE(a.Particles().size()==128);
    for(std::size_t i=0;i<128;++i)
    {
        const auto& x=a.Particles()[i];const auto& y=b.Particles()[i];
        CHECK(x.Velocity.X==y.Velocity.X);CHECK(x.Velocity.Y==y.Velocity.Y);
        CHECK(x.Velocity.Z==y.Velocity.Z);CHECK(x.RemainingLife==y.RemainingLife);
        CHECK(x.RemainingLife>=.25);CHECK(x.RemainingLife<=1.);
    }
    const auto retained=a.Particles()[64];
    burst.Count=64;burst.Seed=4;a.EmitBurst(burst);
    REQUIRE(a.Particles().size()==128);
    CHECK(a.Particles().front().Velocity.X==retained.Velocity.X);
    burst.Count=129;
    CHECK_THROWS_AS(a.EmitBurst(burst),std::invalid_argument);
    CHECK(a.Particles().size()==128);
    a.Update(1.);
    CHECK(a.Particles().empty());
}
TEST_CASE("invalid particle values and integration overflow are rejected")
{
    using namespace kairo::engine;
    ParticleEmitter emitter(4);
    const auto inf=std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(emitter.Emit({{inf,0,0},{},1.}),std::invalid_argument);
    const auto max=std::numeric_limits<double>::max();
    emitter.Emit({{max,0,0},{max,0,0},4.});
    CHECK_THROWS_AS(emitter.Update(2.),std::overflow_error);
    REQUIRE(emitter.Particles().size()==1);
    CHECK(emitter.Particles()[0].Position.X==max);
    CHECK(emitter.Particles()[0].RemainingLife==4.);
    emitter.Update(4.);
    CHECK(emitter.Particles().empty());
}
