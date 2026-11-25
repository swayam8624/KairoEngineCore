module;

#include "detail/SeededHeightfield.hpp"
#include "detail/SeededTerrainMesh.hpp"
#include "detail/SeededTerrainLod.hpp"
#include "detail/SeededTerrainLodError.hpp"
#include "detail/TerrainLodSelection.hpp"
#include "detail/SeededTerrainSurface.hpp"
#include "detail/ProceduralCellDiscovery.hpp"
#include "detail/ProceduralTerrainStore.hpp"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.EngineCore.WorldStreaming;

import Kairo.Foundation.Math.Vector;
import Kairo.Assets.MeshArtifact;

export namespace kairo::engine
{
    struct WorldCellCoordinate final
    {
        std::int32_t X = 0;
        std::int32_t Z = 0;

        friend constexpr auto operator<=>(WorldCellCoordinate,
            WorldCellCoordinate) noexcept = default;
    };

    // Versioned procedural terrain source. Heights are signed integer
    // millimeters, not platform-dependent floating-point RNG results.
    // The world-space spacing between samples is a caller's mesh concern.
    inline constexpr std::uint32_t SeededHeightfieldVersion = 1u;
    struct SeededHeightfieldConfig final
    {
        std::uint32_t Intervals = 32u;
        std::uint32_t LatticeSpacing = 16u;
        std::int32_t MinimumMillimeters = -10000;
        std::int32_t MaximumMillimeters = 30000;
    };

    struct SeededHeightfieldCell final
    {
        WorldCellCoordinate Coordinate;
        std::uint32_t Intervals = 0u;
        // Row-major Z then X, including the shared final row and column.
        std::vector<std::int32_t> HeightsMillimeters;
    };

    [[nodiscard]] inline std::uint64_t DeriveWorldCellSeed(
        std::uint64_t worldSeed, WorldCellCoordinate coordinate,
        std::uint64_t domain = 0u) noexcept
    {
        return detail::WorldCoordinateBits(worldSeed,coordinate.X,coordinate.Z,domain);
    }

    [[nodiscard]] inline SeededHeightfieldCell BuildSeededHeightfieldCell(
        std::uint64_t worldSeed,WorldCellCoordinate coordinate,
        const SeededHeightfieldConfig& config = {})
    {
        return {coordinate,config.Intervals,
            detail::GenerateSeededHeightCell(worldSeed,coordinate.X,coordinate.Z,
                {config.Intervals,config.LatticeSpacing,
                 config.MinimumMillimeters,config.MaximumMillimeters})};
    }

    struct SeededTerrainMeshCell final
    {
        WorldCellCoordinate Coordinate;
        // Double-precision origin; vertices remain cell-local float meters.
        // Large-world floating origin is the consumer's responsibility.
        kairo::foundation::math::Vec3d Origin{};
        kairo::assets::MeshArtifactData Mesh;
    };

    [[nodiscard]] inline SeededTerrainMeshCell BuildSeededTerrainMeshCell(
        std::uint64_t worldSeed, WorldCellCoordinate coordinate,
        const SeededHeightfieldConfig& config = {},
        std::uint32_t sampleSpacingMillimeters = 1000u)
    {
        auto mesh = detail::GenerateSeededTerrainMesh<kairo::assets::MeshArtifactData>(
            worldSeed,coordinate.X,coordinate.Z,
            {config.Intervals,config.LatticeSpacing,
             config.MinimumMillimeters,config.MaximumMillimeters},sampleSpacingMillimeters);
        // Reject numerically degenerate extreme slope/spacing combinations,
        // rather than publishing a noncanonical renderer/asset payload.
        kairo::assets::ValidateMeshArtifactData(mesh);
        const double extent = static_cast<double>(config.Intervals)*sampleSpacingMillimeters/1000.0;
        return {coordinate,{coordinate.X*extent,0.0,coordinate.Z*extent},std::move(mesh)};
    }

    // Rendering approximation only. Physics/grounding queries retain the
    // full-resolution canonical terrain surface regardless of this stride.
    [[nodiscard]] inline SeededTerrainMeshCell BuildSeededTerrainLodMeshCell(
        std::uint64_t worldSeed,WorldCellCoordinate coordinate,
        const SeededHeightfieldConfig& config={},std::uint32_t sampleSpacingMillimeters=1000u,
        std::uint32_t stride=1u)
    {
        auto mesh=detail::GenerateSeededTerrainLod<kairo::assets::MeshArtifactData>(
            worldSeed,coordinate.X,coordinate.Z,
            {config.Intervals,config.LatticeSpacing,config.MinimumMillimeters,config.MaximumMillimeters},
            sampleSpacingMillimeters,stride);
        kairo::assets::ValidateMeshArtifactData(mesh);
        const double extent=static_cast<double>(config.Intervals)*sampleSpacingMillimeters/1000.0;
        return {coordinate,{coordinate.X*extent,0.0,coordinate.Z*extent},std::move(mesh)};
    }

    // Exact rounded-up maximum vertical error of the nominal LOD mesh
    // relative to the full-resolution triangulated heightfield, in mm.
    // Does not include GPU quantization or screen-space projection error.
    [[nodiscard]] inline std::uint64_t MeasureSeededTerrainLodErrorMillimeters(
        std::uint64_t worldSeed,WorldCellCoordinate coordinate,
        const SeededHeightfieldConfig& config={},std::uint32_t stride=1u)
    {
        return detail::MeasureSeededTerrainLodError(worldSeed,coordinate.X,coordinate.Z,
            {config.Intervals,config.LatticeSpacing,config.MinimumMillimeters,config.MaximumMillimeters},
            stride);
    }

    struct TerrainLodErrorChoice final
    {
        std::uint32_t Stride=1u;
        std::uint64_t MaximumErrorMillimeters=0u;
    };

    // Select the coarsest supported mesh under a geometric error ceiling.
    // Do not assume that errors are monotonic across different fan layouts.
    // Intended for preprocessing or background jobs, not every frame.
    [[nodiscard]] inline TerrainLodErrorChoice ChooseSeededTerrainLodUnderError(
        std::uint64_t worldSeed,WorldCellCoordinate coordinate,
        const SeededHeightfieldConfig& config,std::uint32_t maximumStride,
        std::uint64_t maximumErrorMillimeters)
    {
        detail::SeededHeightConfig{config.Intervals,config.LatticeSpacing,
            config.MinimumMillimeters,config.MaximumMillimeters}.Validate();
        if(maximumStride==0||(maximumStride&(maximumStride-1u))!=0||
            config.Intervals%maximumStride!=0)
            throw std::invalid_argument("Invalid maximum terrain LOD stride.");
        for(auto stride=maximumStride;stride>1u;stride/=2u)
        {
            const auto error=MeasureSeededTerrainLodErrorMillimeters(worldSeed,coordinate,config,stride);
            if(error<=maximumErrorMillimeters)return {stride,error};
        }
        return {};
    }

    struct SeededTerrainSurfaceSample final
    {
        double HeightMeters;
        kairo::foundation::math::Vec3d FaceNormal;
    };

    // Nominal double-precision triangle surface. GPU mesh coordinates are
    // floats; tiny quantization differences are possible. This is a vertical
    // surface query, not a swept collision or physics body.
    [[nodiscard]] inline std::optional<SeededTerrainSurfaceSample> SampleSeededTerrainSurface(
        std::uint64_t worldSeed,WorldCellCoordinate coordinate,
        double localX,double localZ,const SeededHeightfieldConfig& config = {},
        std::uint32_t sampleSpacingMillimeters = 1000u)
    {
        const auto sample=detail::QuerySeededTerrainSurface(worldSeed,coordinate.X,coordinate.Z,
            {config.Intervals,config.LatticeSpacing,
             config.MinimumMillimeters,config.MaximumMillimeters},
             sampleSpacingMillimeters,localX,localZ);
        if(!sample) return std::nullopt;
        return SeededTerrainSurfaceSample{sample->HeightMeters,
            {sample->Normal[0],sample->Normal[1],sample->Normal[2]}};
    }

    enum class WorldCellState : std::uint8_t
    {
        Unloaded,
        Loading,
        Resident,
        Unloading
    };

    struct WorldStreamingCellDescriptor final
    {
        WorldCellCoordinate Coordinate;
        std::string ContentKey;
        std::uint64_t EstimatedResidentBytes = 1u;
        std::int32_t Priority = 0;
        bool AlwaysLoaded = false;

        friend bool operator==(const WorldStreamingCellDescriptor&,
            const WorldStreamingCellDescriptor&) = default;

        void Validate() const
        {
            if (ContentKey.empty() || ContentKey.size() > 4096u)
                throw std::invalid_argument(
                    "World streaming cell content key must contain 1..4096 bytes.");
            if (EstimatedResidentBytes == 0u)
                throw std::invalid_argument(
                    "World streaming cell estimated resident bytes must be non-zero.");
        }
    };

    struct WorldStreamingConfig final
    {
        double CellSize = 256.0;
        double LoadRadius = 768.0;
        double KeepRadius = 1024.0;
        std::size_t MaximumLoadsPerUpdate = 4u;
        std::size_t MaximumUnloadsPerUpdate = 8u;
        std::size_t MaximumCommittedCells = 512u;
        std::uint64_t MaximumCommittedBytes = 4ull * 1024ull * 1024ull * 1024ull;

        void Validate() const
        {
            if (!std::isfinite(CellSize) || CellSize <= 0.0)
                throw std::invalid_argument(
                    "World streaming cell size must be finite and positive.");
            if (!std::isfinite(LoadRadius) || LoadRadius < 0.0)
                throw std::invalid_argument(
                    "World streaming load radius must be finite and non-negative.");
            if (!std::isfinite(KeepRadius) || KeepRadius < LoadRadius)
                throw std::invalid_argument(
                    "World streaming keep radius must be finite and at least the load radius.");
            if (MaximumCommittedCells == 0u)
                throw std::invalid_argument(
                    "World streaming committed-cell budget must be non-zero.");
            if (MaximumCommittedBytes == 0u)
                throw std::invalid_argument(
                    "World streaming byte budget must be non-zero.");
        }
    };

    struct WorldStreamingObserver final
    {
        kairo::foundation::math::Vec3d Position{};
        double RadiusScale = 1.0;

        void Validate() const
        {
            if (!std::isfinite(Position.x) || !std::isfinite(Position.y) ||
                !std::isfinite(Position.z))
                throw std::invalid_argument(
                    "World streaming observer position must be finite.");
            if (!std::isfinite(RadiusScale) || RadiusScale <= 0.0)
                throw std::invalid_argument(
                    "World streaming observer radius scale must be finite and positive.");
        }
    };

    struct TerrainLodPolicy final
    {
        // Empty by default: full-resolution geometry everywhere.
        std::vector<double> SwitchDistances;
        double Hysteresis = 0.1;
    };

    [[nodiscard]] inline std::uint32_t SelectSeededTerrainLodStride(
        WorldCellCoordinate coordinate,double cellExtent,std::uint32_t intervals,
        std::span<const WorldStreamingObserver> observers,const TerrainLodPolicy& policy,
        std::uint32_t currentStride=0)
    {
        for(const auto& observer:observers)observer.Validate();
        return detail::SelectTerrainLodStride(coordinate.X,coordinate.Z,cellExtent,
            intervals,observers,policy.SwitchDistances,policy.Hysteresis,currentStride);
    }

    struct WorldStreamingRequest final
    {
        WorldCellCoordinate Coordinate;
        std::string ContentKey;
        std::uint64_t EstimatedResidentBytes = 0u;
        std::int32_t Priority = 0;
        double DistanceSquared = std::numeric_limits<double>::infinity();
        bool AlwaysLoaded = false;

        friend bool operator==(const WorldStreamingRequest&,
            const WorldStreamingRequest&) = default;
    };

    struct WorldStreamingPlan final
    {
        std::vector<WorldStreamingRequest> Loads;
        std::vector<WorldStreamingRequest> Unloads;
        std::size_t CommittedCells = 0u;
        std::uint64_t CommittedBytes = 0u;
        bool BudgetConstrained = false;

        [[nodiscard]] bool Empty() const noexcept
        {
            return Loads.empty() && Unloads.empty();
        }
    };

    struct WorldStreamingCellSnapshot final
    {
        WorldStreamingCellDescriptor Descriptor;
        WorldCellState State = WorldCellState::Unloaded;

        friend bool operator==(const WorldStreamingCellSnapshot&,
            const WorldStreamingCellSnapshot&) = default;
    };

    /// Deterministic policy/state machine for large-world cell residency.
    ///
    /// This type intentionally performs no file I/O and owns no spatial index.
    /// KairoSpatial remains responsible for geometric acceleration, while the
    /// Player/asset layer consumes the emitted load/unload requests and reports
    /// asynchronous completion back through CompleteLoad/CompleteUnload.
    /// Loading and unloading cells count against committed budgets until their
    /// operation completes, preventing transient memory oversubscription.
    class WorldStreamingRuntime final
    {
    public:
        explicit WorldStreamingRuntime(WorldStreamingConfig config = {})
            : m_Config(std::move(config))
        {
            m_Config.Validate();
        }

        [[nodiscard]] const WorldStreamingConfig& Config() const noexcept
        {
            return m_Config;
        }

        void RegisterCell(WorldStreamingCellDescriptor descriptor)
        {
            descriptor.Validate();
            if (!m_Cells.emplace(descriptor.Coordinate,
                CellRecord{ std::move(descriptor), WorldCellState::Unloaded }).second)
                throw std::invalid_argument(
                    "World streaming cell coordinate is already registered.");
        }

        bool UnregisterCell(WorldCellCoordinate coordinate)
        {
            const auto found = m_Cells.find(coordinate);
            if (found == m_Cells.end()) return false;
            if (found->second.State != WorldCellState::Unloaded)
                throw std::logic_error(
                    "A committed world streaming cell cannot be unregistered.");
            m_Cells.erase(found);
            return true;
        }

        [[nodiscard]] bool Contains(WorldCellCoordinate coordinate) const noexcept
        {
            return m_Cells.contains(coordinate);
        }

        [[nodiscard]] WorldCellState State(WorldCellCoordinate coordinate) const
        {
            return Require(coordinate).State;
        }

        [[nodiscard]] const WorldStreamingCellDescriptor& Descriptor(
            WorldCellCoordinate coordinate) const
        {
            return Require(coordinate).Descriptor;
        }

        [[nodiscard]] std::size_t CellCount() const noexcept
        {
            return m_Cells.size();
        }

        [[nodiscard]] std::size_t CommittedCellCount() const noexcept
        {
            std::size_t count = 0u;
            for (const auto& [coordinate, record] : m_Cells)
            {
                (void)coordinate;
                if (record.State != WorldCellState::Unloaded) ++count;
            }
            return count;
        }

        [[nodiscard]] std::uint64_t CommittedBytes() const
        {
            std::uint64_t bytes = 0u;
            for (const auto& [coordinate, record] : m_Cells)
            {
                (void)coordinate;
                if (record.State == WorldCellState::Unloaded) continue;
                if (record.Descriptor.EstimatedResidentBytes >
                    std::numeric_limits<std::uint64_t>::max() - bytes)
                    throw std::overflow_error(
                        "World streaming committed byte accounting overflowed.");
                bytes += record.Descriptor.EstimatedResidentBytes;
            }
            return bytes;
        }

        [[nodiscard]] std::vector<WorldStreamingCellSnapshot> Snapshot() const
        {
            std::vector<WorldStreamingCellSnapshot> result;
            result.reserve(m_Cells.size());
            for (const auto& [coordinate, record] : m_Cells)
            {
                (void)coordinate;
                result.push_back({ record.Descriptor, record.State });
            }
            return result;
        }

        /// Computes one bounded streaming transaction and reserves every
        /// emitted request immediately by moving cells into Loading/Unloading.
        /// This prevents duplicate requests while asynchronous I/O is in flight.
        [[nodiscard]] WorldStreamingPlan PlanUpdate(
            std::span<const WorldStreamingObserver> observers)
        {
            for (const auto& observer : observers) observer.Validate();

            struct Candidate final
            {
                WorldCellCoordinate Coordinate;
                double DistanceSquared = std::numeric_limits<double>::infinity();
            };

            std::vector<Candidate> loadCandidates;
            std::vector<Candidate> unloadCandidates;
            loadCandidates.reserve(m_Cells.size());
            unloadCandidates.reserve(m_Cells.size());

            for (const auto& [coordinate, record] : m_Cells)
            {
                const DistanceDecision decision = Evaluate(record.Descriptor, observers);
                if (record.State == WorldCellState::Unloaded && decision.WantsLoad)
                    loadCandidates.push_back({ coordinate, decision.DistanceSquared });
                else if (record.State == WorldCellState::Resident && !decision.WantsKeep)
                    unloadCandidates.push_back({ coordinate, decision.DistanceSquared });
            }

            std::ranges::sort(loadCandidates, [&](const Candidate& left,
                const Candidate& right)
            {
                const auto& a = Require(left.Coordinate).Descriptor;
                const auto& b = Require(right.Coordinate).Descriptor;
                if (a.AlwaysLoaded != b.AlwaysLoaded) return a.AlwaysLoaded > b.AlwaysLoaded;
                if (a.Priority != b.Priority) return a.Priority > b.Priority;
                if (left.DistanceSquared != right.DistanceSquared)
                    return left.DistanceSquared < right.DistanceSquared;
                return left.Coordinate < right.Coordinate;
            });

            std::ranges::sort(unloadCandidates, [&](const Candidate& left,
                const Candidate& right)
            {
                const auto& a = Require(left.Coordinate).Descriptor;
                const auto& b = Require(right.Coordinate).Descriptor;
                if (left.DistanceSquared != right.DistanceSquared)
                    return left.DistanceSquared > right.DistanceSquared;
                if (a.Priority != b.Priority) return a.Priority < b.Priority;
                return left.Coordinate < right.Coordinate;
            });

            WorldStreamingPlan plan;
            const std::size_t unloadCount = std::min(
                unloadCandidates.size(), m_Config.MaximumUnloadsPerUpdate);
            plan.Unloads.reserve(unloadCount);
            for (std::size_t index = 0u; index < unloadCount; ++index)
            {
                const CellRecord& record = Require(unloadCandidates[index].Coordinate);
                plan.Unloads.push_back(MakeRequest(record.Descriptor,
                    unloadCandidates[index].DistanceSquared));
            }

            std::size_t committedCells = CommittedCellCount();
            std::uint64_t committedBytes = CommittedBytes();
            plan.Loads.reserve(std::min(loadCandidates.size(),
                m_Config.MaximumLoadsPerUpdate));

            for (const Candidate& candidate : loadCandidates)
            {
                if (plan.Loads.size() >= m_Config.MaximumLoadsPerUpdate)
                {
                    plan.BudgetConstrained = true;
                    break;
                }

                const CellRecord& record = Require(candidate.Coordinate);
                const std::uint64_t bytes = record.Descriptor.EstimatedResidentBytes;
                const bool cellBudgetAvailable =
                    committedCells < m_Config.MaximumCommittedCells;
                const bool byteBudgetAvailable = bytes <=
                    m_Config.MaximumCommittedBytes -
                        std::min(committedBytes, m_Config.MaximumCommittedBytes);
                if (!cellBudgetAvailable || !byteBudgetAvailable)
                {
                    plan.BudgetConstrained = true;
                    continue;
                }

                ++committedCells;
                committedBytes += bytes;
                plan.Loads.push_back(MakeRequest(record.Descriptor,
                    candidate.DistanceSquared));
            }

            if (plan.Loads.size() < loadCandidates.size())
                plan.BudgetConstrained = true;
            plan.CommittedCells = committedCells;
            plan.CommittedBytes = committedBytes;
            // All allocating request construction finished. Publish the
            // reservations only now, so an exception cannot strand a cell
            // in Loading/Unloading without a delivered request.
            for (const auto& request : plan.Unloads)
                RequireMutable(request.Coordinate).State = WorldCellState::Unloading;
            for (const auto& request : plan.Loads)
                RequireMutable(request.Coordinate).State = WorldCellState::Loading;
            return plan;
        }

        void CompleteLoad(WorldCellCoordinate coordinate, bool success)
        {
            CellRecord& record = RequireMutable(coordinate);
            if (record.State != WorldCellState::Loading)
                throw std::logic_error(
                    "World streaming load completion requires a Loading cell.");
            record.State = success ? WorldCellState::Resident : WorldCellState::Unloaded;
        }

        void CompleteUnload(WorldCellCoordinate coordinate, bool success)
        {
            CellRecord& record = RequireMutable(coordinate);
            if (record.State != WorldCellState::Unloading)
                throw std::logic_error(
                    "World streaming unload completion requires an Unloading cell.");
            record.State = success ? WorldCellState::Unloaded : WorldCellState::Resident;
        }

        void Reset()
        {
            for (auto& [coordinate, record] : m_Cells)
            {
                (void)coordinate;
                record.State = WorldCellState::Unloaded;
            }
        }

    private:
        struct CellRecord final
        {
            WorldStreamingCellDescriptor Descriptor;
            WorldCellState State = WorldCellState::Unloaded;
        };

        struct DistanceDecision final
        {
            double DistanceSquared = std::numeric_limits<double>::infinity();
            bool WantsLoad = false;
            bool WantsKeep = false;
        };

        WorldStreamingConfig m_Config;
        std::map<WorldCellCoordinate, CellRecord> m_Cells;

        [[nodiscard]] const CellRecord& Require(WorldCellCoordinate coordinate) const
        {
            const auto found = m_Cells.find(coordinate);
            if (found == m_Cells.end())
                throw std::out_of_range(
                    "World streaming cell coordinate is not registered.");
            return found->second;
        }

        [[nodiscard]] CellRecord& RequireMutable(WorldCellCoordinate coordinate)
        {
            const auto found = m_Cells.find(coordinate);
            if (found == m_Cells.end())
                throw std::out_of_range(
                    "World streaming cell coordinate is not registered.");
            return found->second;
        }

        [[nodiscard]] static WorldStreamingRequest MakeRequest(
            const WorldStreamingCellDescriptor& descriptor, double distanceSquared)
        {
            return {
                descriptor.Coordinate,
                descriptor.ContentKey,
                descriptor.EstimatedResidentBytes,
                descriptor.Priority,
                distanceSquared,
                descriptor.AlwaysLoaded
            };
        }

        [[nodiscard]] double DistanceSquaredToCell(
            WorldCellCoordinate coordinate,
            const WorldStreamingObserver& observer) const noexcept
        {
            const double minimumX = static_cast<double>(coordinate.X) * m_Config.CellSize;
            const double minimumZ = static_cast<double>(coordinate.Z) * m_Config.CellSize;
            const double maximumX = minimumX + m_Config.CellSize;
            const double maximumZ = minimumZ + m_Config.CellSize;
            const double dx = observer.Position.x < minimumX
                ? minimumX - observer.Position.x
                : observer.Position.x > maximumX
                    ? observer.Position.x - maximumX : 0.0;
            const double dz = observer.Position.z < minimumZ
                ? minimumZ - observer.Position.z
                : observer.Position.z > maximumZ
                    ? observer.Position.z - maximumZ : 0.0;
            return dx * dx + dz * dz;
        }

        [[nodiscard]] DistanceDecision Evaluate(
            const WorldStreamingCellDescriptor& descriptor,
            std::span<const WorldStreamingObserver> observers) const noexcept
        {
            DistanceDecision decision;
            if (descriptor.AlwaysLoaded)
            {
                decision.WantsLoad = true;
                decision.WantsKeep = true;
            }

            for (const auto& observer : observers)
            {
                const double distanceSquared = DistanceSquaredToCell(
                    descriptor.Coordinate, observer);
                decision.DistanceSquared = std::min(decision.DistanceSquared,
                    distanceSquared);
                const double loadRadius = m_Config.LoadRadius * observer.RadiusScale;
                const double keepRadius = m_Config.KeepRadius * observer.RadiusScale;
                if (distanceSquared <= loadRadius * loadRadius)
                    decision.WantsLoad = true;
                if (distanceSquared <= keepRadius * keepRadius)
                    decision.WantsKeep = true;
            }
            return decision;
        }
    };

    // A generated world is a bounded registry around current observers,
    // not a manifest containing every cell ever visited. The existing
    // WorldStreamingRuntime remains the sole authority on residency.
    struct ProceduralWorldConfig final
    {
        std::uint64_t Seed = 0u;
        SeededHeightfieldConfig Terrain;
        std::uint32_t SampleSpacingMillimeters = 1000u;
        WorldStreamingConfig Streaming;
        std::size_t MaximumScannedCells = 65536u;
        // Estimate includes consumer-owned resources; this is not actual
        // GPU memory telemetry. Zero selects canonical CPU mesh payload.
        std::uint64_t EstimatedResidentBytes = 0u;
    };

    class ProceduralWorldStreamingRuntime final
    {
    public:
        explicit ProceduralWorldStreamingRuntime(ProceduralWorldConfig config = {})
            : m_Config(Validate(std::move(config))), m_Runtime(m_Config.Streaming) {}

        [[nodiscard]] const ProceduralWorldConfig& Config() const noexcept { return m_Config; }
        [[nodiscard]] const WorldStreamingRuntime& Runtime() const noexcept { return m_Runtime; }

        [[nodiscard]] WorldStreamingPlan PlanUpdate(std::span<const WorldStreamingObserver> observers)
        {
            for (const auto& observer : observers)
            {
                observer.Validate();
                const double keep=m_Config.Streaming.KeepRadius*observer.RadiusScale;
                if(!std::isfinite(keep)||keep>1.0e100)
                    throw std::invalid_argument("Procedural keep radius exceeds safe range.");
            }
            const auto wanted=detail::DiscoverProceduralCells(m_Config.Streaming.CellSize,
                m_Config.Streaming.LoadRadius,observers,m_Config.MaximumScannedCells);
            // Prune only unloaded cells. Pending/resident ownership remains
            // discoverable until the consumer acknowledges its completion.
            for(const auto& snapshot:m_Runtime.Snapshot())
            {
                const auto coordinate=snapshot.Descriptor.Coordinate;
                if(snapshot.State==WorldCellState::Unloaded &&
                    !wanted.contains({coordinate.X,coordinate.Z}))
                    m_Runtime.UnregisterCell(coordinate);
            }
            for(const auto& [x,z]:wanted)
            {
                const WorldCellCoordinate coordinate{x,z};
                if(!m_Runtime.Contains(coordinate))
                    m_Runtime.RegisterCell({coordinate,ContentKey(coordinate),
                        m_Config.EstimatedResidentBytes,0,false});
            }
            return m_Runtime.PlanUpdate(observers);
        }

        void CompleteLoad(WorldCellCoordinate coordinate,bool success)
        { m_Runtime.CompleteLoad(coordinate,success); }
        void CompleteUnload(WorldCellCoordinate coordinate,bool success)
        { m_Runtime.CompleteUnload(coordinate,success); }

        [[nodiscard]] SeededTerrainMeshCell BuildMesh(WorldCellCoordinate coordinate) const
        {
            return BuildSeededTerrainMeshCell(m_Config.Seed,coordinate,m_Config.Terrain,
                m_Config.SampleSpacingMillimeters);
        }

    private:
        ProceduralWorldConfig m_Config;
        WorldStreamingRuntime m_Runtime;

        static ProceduralWorldConfig Validate(ProceduralWorldConfig config)
        {
            detail::SeededHeightConfig{config.Terrain.Intervals,config.Terrain.LatticeSpacing,
                config.Terrain.MinimumMillimeters,config.Terrain.MaximumMillimeters}.Validate();
            if(config.SampleSpacingMillimeters==0||config.SampleSpacingMillimeters>1000000||
                config.MaximumScannedCells==0||config.MaximumScannedCells>1000000)
                throw std::invalid_argument("Invalid procedural cell spacing or scan budget.");
            const auto n=static_cast<std::uint64_t>(config.Terrain.Intervals);
            config.Streaming.CellSize=static_cast<double>(n)*config.SampleSpacingMillimeters/1000.0;
            config.Streaming.Validate();
            if(config.Streaming.KeepRadius>1.0e100)
                throw std::invalid_argument("Procedural keep radius exceeds safe range.");
            if(config.EstimatedResidentBytes==0)
                config.EstimatedResidentBytes=(n+1u)*(n+1u)*sizeof(kairo::assets::MeshArtifactVertex)+
                    6u*n*n*sizeof(std::uint32_t);
            return config;
        }

        [[nodiscard]] std::string ContentKey(WorldCellCoordinate c) const
        {
            const auto& t=m_Config.Terrain;
            return "procedural://heightfield/v1/"+std::to_string(m_Config.Seed)+"/"+
                std::to_string(t.Intervals)+"/"+std::to_string(t.LatticeSpacing)+"/"+
                std::to_string(t.MinimumMillimeters)+"/"+std::to_string(t.MaximumMillimeters)+"/"+
                std::to_string(m_Config.SampleSpacingMillimeters)+"/"+
                std::to_string(c.X)+"/"+std::to_string(c.Z);
        }
    };


    struct ProceduralTerrainUpdate final
    {
        WorldStreamingPlan Plan; // reservation-time accounting
        std::size_t Loaded=0,Unloaded=0,FailedLoads=0,FailedUnloads=0;
        std::exception_ptr FirstFailure;

        [[nodiscard]] bool Succeeded() const noexcept
        { return FailedLoads==0 && FailedUnloads==0; }
    };

    // Synchronous CPU terrain owner. A renderer can consume its canonical
    // mesh artifacts; GPU and physics ownership are intentionally not here.
    // Do not retain a pointer/reference to an entry across an Update that
    // may unload that coordinate.
    class ProceduralTerrainRuntime final
    {
    public:
        explicit ProceduralTerrainRuntime(ProceduralWorldConfig config={})
            : m_Streaming(std::move(config)) {}

        [[nodiscard]] const ProceduralWorldStreamingRuntime& Streaming() const noexcept
        { return m_Streaming; }

        [[nodiscard]] const std::map<WorldCellCoordinate,SeededTerrainMeshCell>& ResidentCells() const noexcept
        { return m_Store.Cells(); }

        [[nodiscard]] const SeededTerrainMeshCell* Find(WorldCellCoordinate coordinate) const noexcept
        { return m_Store.Find(coordinate); }

        [[nodiscard]] ProceduralTerrainUpdate Update(std::span<const WorldStreamingObserver> observers)
        {
            auto plan=m_Streaming.PlanUpdate(observers);
            const auto stats=detail::ApplyProceduralTerrainPlan(m_Store,m_Streaming,plan,
                [this](WorldCellCoordinate coordinate){return m_Streaming.BuildMesh(coordinate);});
            return {std::move(plan),stats.Loaded,stats.Unloaded,
                stats.FailedLoads,stats.FailedUnloads,stats.FirstFailure};
        }

    private:
        ProceduralWorldStreamingRuntime m_Streaming;
        detail::ProceduralTerrainStore<WorldCellCoordinate,SeededTerrainMeshCell> m_Store;
    };

}
