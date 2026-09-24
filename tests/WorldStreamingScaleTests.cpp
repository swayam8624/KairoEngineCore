#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

import Kairo.EngineCore.WorldStreaming;
import Kairo.Foundation.Math.Vector;

using namespace kairo::engine;

namespace
{
    struct StreamingStressResult
    {
        std::vector<WorldStreamingCellSnapshot> Snapshot;
        std::size_t PeakCells = 0u;
        std::uint64_t PeakBytes = 0u;
    };

    StreamingStressResult RunStreamingStress()
    {
        WorldStreamingConfig config;
        config.CellSize = 64.0;
        config.LoadRadius = 220.0;
        config.KeepRadius = 320.0;
        config.MaximumLoadsPerUpdate = 24u;
        config.MaximumUnloadsPerUpdate = 48u;
        config.MaximumCommittedCells = 96u;
        config.MaximumCommittedBytes = 96u * 4096u;

        WorldStreamingRuntime runtime(config);
        constexpr int side = 64;
        for (int z = 0; z < side; ++z)
            for (int x = 0; x < side; ++x)
                runtime.RegisterCell({
                    { x, z },
                    "world/" + std::to_string(x) + "_" + std::to_string(z) + ".cell",
                    4096u,
                    (x + z) % 5,
                    false });

        StreamingStressResult result;
        for (int step = 0; step < 96; ++step)
        {
            const double x = 32.0 + static_cast<double>((step * 41) % (side * 64));
            const double z = 32.0 + static_cast<double>((step * 67) % (side * 64));
            const std::array observers{
                WorldStreamingObserver{
                    kairo::foundation::math::Vec3d{ x, 0.0, z }, 1.0 }
            };

            const WorldStreamingPlan plan = runtime.PlanUpdate(observers);
            for (const auto& request : plan.Unloads)
                runtime.CompleteUnload(request.Coordinate, true);
            for (const auto& request : plan.Loads)
                runtime.CompleteLoad(request.Coordinate, true);

            result.PeakCells = std::max(result.PeakCells, runtime.CommittedCellCount());
            result.PeakBytes = std::max(result.PeakBytes, runtime.CommittedBytes());
            CHECK(runtime.CommittedCellCount() <= config.MaximumCommittedCells);
            CHECK(runtime.CommittedBytes() <= config.MaximumCommittedBytes);
        }

        result.Snapshot = runtime.Snapshot();
        return result;
    }
}

TEST_CASE("large world streaming remains budget-bounded and deterministic",
    "[EngineCore][WorldStreaming][Scale]")
{
    const auto first = RunStreamingStress();
    const auto second = RunStreamingStress();

    CHECK(first.PeakCells <= 96u);
    CHECK(first.PeakBytes <= 96u * 4096u);
    CHECK(first.PeakCells == second.PeakCells);
    CHECK(first.PeakBytes == second.PeakBytes);
    CHECK(first.Snapshot == second.Snapshot);
}
