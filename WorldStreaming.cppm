module;

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.EngineCore.WorldStreaming;

import Kairo.Foundation.Math.Vector;

export namespace kairo::engine
{
    struct WorldCellCoordinate final
    {
        std::int32_t X = 0;
        std::int32_t Z = 0;

        friend constexpr auto operator<=>(WorldCellCoordinate,
            WorldCellCoordinate) noexcept = default;
    };

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
                CellRecord& record = RequireMutable(unloadCandidates[index].Coordinate);
                record.State = WorldCellState::Unloading;
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

                CellRecord& record = RequireMutable(candidate.Coordinate);
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

                record.State = WorldCellState::Loading;
                ++committedCells;
                committedBytes += bytes;
                plan.Loads.push_back(MakeRequest(record.Descriptor,
                    candidate.DistanceSquared));
            }

            if (plan.Loads.size() < loadCandidates.size())
                plan.BudgetConstrained = true;
            plan.CommittedCells = committedCells;
            plan.CommittedBytes = committedBytes;
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
}
