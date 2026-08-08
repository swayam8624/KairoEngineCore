#include <filesystem>

#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.ProductionRuntime;
import Kairo.EngineCore.ProductionSystemsManifest;

TEST_CASE("production systems manifest round trips every Phase 12 subsystem")
{
    using namespace kairo::engine;
    ProductionSystemsManifest manifest;
    ProductionAnimationDescriptor animation;
    animation.Name = "Walk";
    animation.Duration = 1.0;
    animation.Keys.push_back({ "Root", { 0.0, { 0.0, 0.0, 0.0 } } });
    animation.Keys.push_back({ "Root", { 1.0, { 1.0, 0.0, 0.0 } } });
    manifest.Animations.push_back(std::move(animation));
    manifest.Terrain = ProductionTerrainDescriptor{ 33u, 33u, 2.0 };
    manifest.Foliage = ProductionFoliageDescriptor{ 128u, 42u };
    manifest.Particles = ProductionParticleDescriptor{ 512u };
    manifest.Cloth = ProductionClothDescriptor{ 256u, 5u };
    manifest.Fluid = ProductionFluidDescriptor{ 32u, 32u, 0.05 };
    manifest.Streaming = ProductionStreamingDescriptor{ 64.0, 2 };

    const std::string first = SerializeProductionSystemsManifest(manifest);
    const auto restored = ParseProductionSystemsManifest(first);
    CHECK(SerializeProductionSystemsManifest(restored) == first);
    const auto workload = EstimateProductionWorkload(restored);
    CHECK(workload.AnimationKeys == 2u);
    CHECK(workload.TerrainCells == 1089u);
    CHECK(workload.FoliageInstances == 128u);
    CHECK(workload.ParticleCapacity == 512u);
    CHECK(workload.FluidCells == 1024u);
    CHECK(workload.StreamingCells == 25u);
}

TEST_CASE("production manifest rejects configurations above explicit budgets")
{
    using namespace kairo::engine;
    ProductionSystemsManifest manifest;
    manifest.Particles = ProductionParticleDescriptor{ 100u };
    ProductionPerformanceBudget budget;
    budget.MaximumParticles = 99u;
    CHECK_THROWS_AS(ValidateProductionSystemsManifest(manifest, budget), std::length_error);
}

TEST_CASE("production runtime orchestrates animation particles fluid streaming and profiling")
{
    using namespace kairo::engine;
    ProductionSystemsManifest manifest;
    ProductionAnimationDescriptor animation;
    animation.Name = "Move";
    animation.Duration = 1.0;
    animation.Keys.push_back({ "Root", { 0.0, { 0.0, 0.0, 0.0 } } });
    animation.Keys.push_back({ "Root", { 1.0, { 2.0, 0.0, 0.0 } } });
    manifest.Animations.push_back(std::move(animation));
    manifest.Terrain = ProductionTerrainDescriptor{ 8u, 8u, 1.0 };
    manifest.Foliage = ProductionFoliageDescriptor{ 16u, 9u };
    manifest.Particles = ProductionParticleDescriptor{ 8u };
    manifest.Fluid = ProductionFluidDescriptor{ 8u, 8u, 0.05 };
    manifest.Streaming = ProductionStreamingDescriptor{ 32.0, 1 };

    ProductionRuntime runtime(manifest);
    REQUIRE(runtime.HasTerrain());
    CHECK(runtime.Foliage().size() == 16u);
    const auto sampled = runtime.SampleAnimation("Move", "Root", 0.5, false);
    CHECK(sampled.X == 1.0);
    runtime.EmitParticle({ {}, { 1.0, 0.0, 0.0 }, 1.0 });
    runtime.Fluid()->AddDensity(4u, 4u, 1.0);
    runtime.Step(0.1, 64.0, 0.0);
    REQUIRE(runtime.Particles() != nullptr);
    REQUIRE(runtime.Particles()->Particles().size() == 1u);
    CHECK(runtime.Particles()->Particles().front().Position.X == 0.1);
    CHECK(runtime.Streaming()->Loaded().size() == 9u);
    CHECK(runtime.Profile().Frames == 1u);
    CHECK(runtime.Profile().AnimationSamples == 1u);
    CHECK(runtime.Profile().FluidCellUpdates == 64u);
    CHECK(runtime.Profile().PeakLoadedCells == 9u);
}

TEST_CASE("production system manifest saves and reopens as project-owned config")
{
    using namespace kairo::engine;
    ProductionSystemsManifest manifest;
    manifest.Particles = ProductionParticleDescriptor{ 64u };
    manifest.Streaming = ProductionStreamingDescriptor{ 128.0, 2 };
    const auto root = std::filesystem::temp_directory_path() / "kairo-production-manifest-test";
    std::filesystem::remove_all(root);
    const auto path = root / DefaultProductionSystemsManifestPath;
    SaveProductionSystemsManifest(path, manifest);
    REQUIRE(std::filesystem::is_regular_file(path));
    const auto restored = LoadProductionSystemsManifest(path);
    CHECK(SerializeProductionSystemsManifest(restored) == SerializeProductionSystemsManifest(manifest));
    std::filesystem::remove_all(root);
}
