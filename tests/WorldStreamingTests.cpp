#include <catch2/catch_test_macros.hpp>

#include <array>
#include <stdexcept>

import Kairo.EngineCore.WorldStreaming;
import Kairo.Foundation.Math.Vector;

namespace
{
    using namespace kairo::engine;

    WorldStreamingConfig TestConfig()
    {
        WorldStreamingConfig config;
        config.CellSize = 100.0;
        config.LoadRadius = 50.0;
        config.KeepRadius = 100.0;
        config.MaximumLoadsPerUpdate = 8u;
        config.MaximumUnloadsPerUpdate = 8u;
        config.MaximumCommittedCells = 16u;
        config.MaximumCommittedBytes = 16'000u;
        return config;
    }

    WorldStreamingCellDescriptor Cell(
        std::int32_t x,
        std::int32_t z,
        const char* key,
        std::uint64_t bytes = 100u,
        std::int32_t priority = 0,
        bool alwaysLoaded = false)
    {
        return { { x, z }, key, bytes, priority, alwaysLoaded };
    }

    WorldStreamingObserver Observer(double x, double z, double scale = 1.0)
    {
        return { kairo::foundation::math::Vec3d{ x, 0.0, z }, scale };
    }
}

TEST_CASE("world streaming uses load and keep radii as hysteresis")
{
    WorldStreamingRuntime runtime(TestConfig());
    runtime.RegisterCell(Cell(0, 0, "world/0_0.cell"));

    const std::array nearObservers{ Observer(50.0, 50.0) };
    const auto load = runtime.PlanUpdate(nearObservers);
    REQUIRE(load.Loads.size() == 1u);
    CHECK(load.Unloads.empty());
    CHECK(load.Loads.front().Coordinate == WorldCellCoordinate{ 0, 0 });
    CHECK(runtime.State({ 0, 0 }) == WorldCellState::Loading);

    runtime.CompleteLoad({ 0, 0 }, true);
    CHECK(runtime.State({ 0, 0 }) == WorldCellState::Resident);
    CHECK(runtime.CommittedCellCount() == 1u);
    CHECK(runtime.CommittedBytes() == 100u);

    // The observer is now outside the 50-unit load radius but still within the
    // 100-unit keep radius. The resident cell must not thrash out of memory.
    const std::array hysteresisObservers{ Observer(160.0, 50.0) };
    const auto keep = runtime.PlanUpdate(hysteresisObservers);
    CHECK(keep.Empty());
    CHECK(runtime.State({ 0, 0 }) == WorldCellState::Resident);

    const std::array farObservers{ Observer(250.0, 50.0) };
    const auto unload = runtime.PlanUpdate(farObservers);
    REQUIRE(unload.Unloads.size() == 1u);
    CHECK(runtime.State({ 0, 0 }) == WorldCellState::Unloading);

    runtime.CompleteUnload({ 0, 0 }, true);
    CHECK(runtime.State({ 0, 0 }) == WorldCellState::Unloaded);
    CHECK(runtime.CommittedCellCount() == 0u);
    CHECK(runtime.CommittedBytes() == 0u);
}

TEST_CASE("world streaming unions demand from multiple observers")
{
    auto config = TestConfig();
    config.LoadRadius = 10.0;
    config.KeepRadius = 20.0;
    WorldStreamingRuntime runtime(config);
    runtime.RegisterCell(Cell(0, 0, "west.cell"));
    runtime.RegisterCell(Cell(10, 0, "east.cell"));
    runtime.RegisterCell(Cell(5, 5, "unused.cell"));

    const std::array observers{
        Observer(50.0, 50.0),
        Observer(1050.0, 50.0)
    };
    const auto plan = runtime.PlanUpdate(observers);
    REQUIRE(plan.Loads.size() == 2u);
    CHECK(plan.Loads[0].Coordinate == WorldCellCoordinate{ 0, 0 });
    CHECK(plan.Loads[1].Coordinate == WorldCellCoordinate{ 10, 0 });
    CHECK(runtime.State({ 5, 5 }) == WorldCellState::Unloaded);
}

TEST_CASE("world streaming orders loads deterministically and obeys committed budgets")
{
    auto config = TestConfig();
    config.LoadRadius = 1'000.0;
    config.KeepRadius = 1'200.0;
    config.MaximumCommittedCells = 2u;
    config.MaximumCommittedBytes = 250u;
    WorldStreamingRuntime runtime(config);

    runtime.RegisterCell(Cell(2, 0, "low.cell", 100u, 0));
    runtime.RegisterCell(Cell(1, 0, "high.cell", 100u, 20));
    runtime.RegisterCell(Cell(0, 0, "medium.cell", 100u, 10));

    const std::array observers{ Observer(50.0, 50.0) };
    const auto plan = runtime.PlanUpdate(observers);
    REQUIRE(plan.Loads.size() == 2u);
    CHECK(plan.Loads[0].ContentKey == "high.cell");
    CHECK(plan.Loads[1].ContentKey == "medium.cell");
    CHECK(plan.BudgetConstrained);
    CHECK(plan.CommittedCells == 2u);
    CHECK(plan.CommittedBytes == 200u);
    CHECK(runtime.State({ 2, 0 }) == WorldCellState::Unloaded);
}

TEST_CASE("always-loaded cells are requested without observers and outrank normal cells")
{
    auto config = TestConfig();
    config.MaximumLoadsPerUpdate = 1u;
    WorldStreamingRuntime runtime(config);
    runtime.RegisterCell(Cell(0, 0, "normal.cell", 100u, 100));
    runtime.RegisterCell(Cell(50, 50, "persistent.cell", 100u, -100, true));

    const auto plan = runtime.PlanUpdate({});
    REQUIRE(plan.Loads.size() == 1u);
    CHECK(plan.Loads.front().ContentKey == "persistent.cell");
    CHECK(plan.Loads.front().AlwaysLoaded);
    runtime.CompleteLoad({ 50, 50 }, true);

    const auto noObservers = runtime.PlanUpdate({});
    CHECK(noObservers.Unloads.empty());
    CHECK(runtime.State({ 50, 50 }) == WorldCellState::Resident);
}

TEST_CASE("in-flight streaming requests are not emitted twice")
{
    WorldStreamingRuntime runtime(TestConfig());
    runtime.RegisterCell(Cell(0, 0, "cell.bin"));
    const std::array observers{ Observer(50.0, 50.0) };

    REQUIRE(runtime.PlanUpdate(observers).Loads.size() == 1u);
    CHECK(runtime.PlanUpdate(observers).Loads.empty());

    runtime.CompleteLoad({ 0, 0 }, false);
    CHECK(runtime.State({ 0, 0 }) == WorldCellState::Unloaded);
    REQUIRE(runtime.PlanUpdate(observers).Loads.size() == 1u);
}

TEST_CASE("streaming lifecycle rejects stale completions and committed unregistration")
{
    WorldStreamingRuntime runtime(TestConfig());
    runtime.RegisterCell(Cell(0, 0, "cell.bin"));

    CHECK_THROWS_AS(runtime.CompleteLoad({ 0, 0 }, true), std::logic_error);
    CHECK_THROWS_AS(runtime.CompleteUnload({ 0, 0 }, true), std::logic_error);

    const std::array observers{ Observer(50.0, 50.0) };
    REQUIRE(runtime.PlanUpdate(observers).Loads.size() == 1u);
    CHECK_THROWS_AS(runtime.UnregisterCell({ 0, 0 }), std::logic_error);

    runtime.CompleteLoad({ 0, 0 }, true);
    const std::array farObservers{ Observer(500.0, 500.0) };
    REQUIRE(runtime.PlanUpdate(farObservers).Unloads.size() == 1u);
    CHECK_THROWS_AS(runtime.CompleteLoad({ 0, 0 }, true), std::logic_error);

    runtime.CompleteUnload({ 0, 0 }, true);
    CHECK(runtime.UnregisterCell({ 0, 0 }));
    CHECK_FALSE(runtime.Contains({ 0, 0 }));
}

TEST_CASE("world streaming validates descriptors observers and budgets")
{
    WorldStreamingRuntime runtime(TestConfig());
    CHECK_THROWS_AS(runtime.RegisterCell(Cell(0, 0, "", 100u)), std::invalid_argument);
    CHECK_THROWS_AS(runtime.RegisterCell(Cell(0, 0, "bad.cell", 0u)), std::invalid_argument);

    runtime.RegisterCell(Cell(0, 0, "valid.cell"));
    CHECK_THROWS_AS(runtime.RegisterCell(Cell(0, 0, "duplicate.cell")), std::invalid_argument);

    auto badObserver = Observer(0.0, 0.0, 0.0);
    const std::array invalidObservers{ badObserver };
    CHECK_THROWS_AS(runtime.PlanUpdate(invalidObservers), std::invalid_argument);

    auto badConfig = TestConfig();
    badConfig.KeepRadius = badConfig.LoadRadius - 1.0;
    CHECK_THROWS_AS(WorldStreamingRuntime(badConfig), std::invalid_argument);
}
