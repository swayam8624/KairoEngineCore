module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.EngineCore.ProductionRuntime;

import Kairo.EngineCore.ProductionSystems;
import Kairo.EngineCore.ProductionSystemsManifest;

export namespace kairo::engine
{
    struct ProductionRuntimeProfile final
    {
        std::uint64_t Frames = 0u;
        std::uint64_t AnimationSamples = 0u;
        std::uint64_t ParticleUpdates = 0u;
        std::uint64_t ClothConstraintIterations = 0u;
        std::uint64_t FluidCellUpdates = 0u;
        std::uint64_t StreamingCellEvaluations = 0u;
        std::size_t PeakParticles = 0u;
        std::size_t PeakLoadedCells = 0u;
    };

    class ProductionRuntime final
    {
    public:
        explicit ProductionRuntime(ProductionSystemsManifest manifest,
            ProductionPerformanceBudget budget = {})
            : m_Manifest(std::move(manifest)), m_Budget(budget)
        {
            ValidateProductionSystemsManifest(m_Manifest, m_Budget);
            for (const auto& descriptor : m_Manifest.Animations)
                m_Animations.emplace(descriptor.Name, BuildAnimationClip(descriptor));
            if (m_Manifest.Terrain)
            {
                m_Terrain.emplace(m_Manifest.Terrain->Width,
                    m_Manifest.Terrain->Height,
                    m_Manifest.Terrain->CellSize);
            }
            if (m_Manifest.Foliage)
            {
                if (!m_Terrain)
                    throw std::invalid_argument("Production foliage requires a terrain descriptor.");
                m_Foliage = ScatterFoliage(*m_Terrain,
                    m_Manifest.Foliage->InstanceCount,
                    m_Manifest.Foliage->Seed);
            }
            if (m_Manifest.Particles)
                m_Particles.emplace(m_Manifest.Particles->Capacity);
            if (m_Manifest.Cloth) m_Cloth.emplace();
            if (m_Manifest.Fluid)
                m_Fluid.emplace(m_Manifest.Fluid->Width, m_Manifest.Fluid->Height);
            if (m_Manifest.Streaming)
                m_Streaming.emplace(m_Manifest.Streaming->CellSize, m_Manifest.Streaming->Radius);
        }

        [[nodiscard]] const ProductionSystemsManifest& Manifest() const noexcept { return m_Manifest; }
        [[nodiscard]] const ProductionRuntimeProfile& Profile() const noexcept { return m_Profile; }
        [[nodiscard]] const ProductionWorkloadEstimate Workload() const
        { return EstimateProductionWorkload(m_Manifest); }

        [[nodiscard]] bool HasTerrain() const noexcept { return m_Terrain.has_value(); }
        [[nodiscard]] TerrainHeightfield& Terrain()
        { if (!m_Terrain) throw std::logic_error("Production runtime has no terrain."); return *m_Terrain; }
        [[nodiscard]] const std::vector<FoliageInstance>& Foliage() const noexcept { return m_Foliage; }
        [[nodiscard]] ParticleEmitter* Particles() noexcept { return m_Particles ? &*m_Particles : nullptr; }
        [[nodiscard]] ClothSimulation* Cloth() noexcept { return m_Cloth ? &*m_Cloth : nullptr; }
        [[nodiscard]] FluidGrid* Fluid() noexcept { return m_Fluid ? &*m_Fluid : nullptr; }
        [[nodiscard]] WorldStreamer* Streaming() noexcept { return m_Streaming ? &*m_Streaming : nullptr; }

        [[nodiscard]] ProductionVec3 SampleAnimation(std::string_view clip,
            std::string_view channel, double time, bool loop = true)
        {
            const auto found = m_Animations.find(std::string(clip));
            if (found == m_Animations.end()) throw std::out_of_range("Production animation clip was not found.");
            ++m_Profile.AnimationSamples;
            return found->second.Sample(channel, time, loop);
        }

        void EmitParticle(Particle particle)
        {
            if (!m_Particles) throw std::logic_error("Production particle system is not configured.");
            m_Particles->Emit(std::move(particle));
            m_Profile.PeakParticles = std::max(m_Profile.PeakParticles, m_Particles->Particles().size());
        }

        std::size_t AddClothParticle(ClothParticle particle)
        {
            if (!m_Cloth || !m_Manifest.Cloth)
                throw std::logic_error("Production cloth system is not configured.");
            if (m_Cloth->Particles().size() >= m_Manifest.Cloth->MaximumParticles)
                throw std::length_error("Production cloth particle budget exceeded.");
            return m_Cloth->AddParticle(particle);
        }

        void AddClothConstraint(std::size_t a, std::size_t b)
        {
            if (!m_Cloth) throw std::logic_error("Production cloth system is not configured.");
            m_Cloth->AddConstraint(a, b);
        }

        void Step(double deltaSeconds, double streamingX = 0.0, double streamingZ = 0.0)
        {
            if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 ||
                !std::isfinite(streamingX) || !std::isfinite(streamingZ))
                throw std::invalid_argument("Production runtime step values must be finite and non-negative.");
            ++m_Profile.Frames;
            if (m_Particles)
            {
                m_Particles->Update(deltaSeconds);
                m_Profile.ParticleUpdates += m_Particles->Particles().size();
                m_Profile.PeakParticles = std::max(m_Profile.PeakParticles, m_Particles->Particles().size());
            }
            if (m_Cloth && m_Manifest.Cloth && !m_Cloth->Particles().empty() && deltaSeconds > 0.0)
            {
                m_Cloth->Step(deltaSeconds, { 0.0, -9.81, 0.0 },
                    m_Manifest.Cloth->ConstraintIterations);
                m_Profile.ClothConstraintIterations +=
                    static_cast<std::uint64_t>(m_Manifest.Cloth->ConstraintIterations) *
                    static_cast<std::uint64_t>(m_Cloth->Particles().size());
            }
            if (m_Fluid && m_Manifest.Fluid)
            {
                m_Fluid->Diffuse(m_Manifest.Fluid->Diffusion);
                m_Profile.FluidCellUpdates += static_cast<std::uint64_t>(m_Manifest.Fluid->Width) *
                    static_cast<std::uint64_t>(m_Manifest.Fluid->Height);
            }
            if (m_Streaming)
            {
                m_Streaming->Update(streamingX, streamingZ);
                m_Profile.StreamingCellEvaluations += m_Streaming->Loaded().size();
                m_Profile.PeakLoadedCells = std::max(m_Profile.PeakLoadedCells,
                    m_Streaming->Loaded().size());
            }
        }

    private:
        ProductionSystemsManifest m_Manifest;
        ProductionPerformanceBudget m_Budget;
        std::map<std::string, AnimationClip> m_Animations;
        std::optional<TerrainHeightfield> m_Terrain;
        std::vector<FoliageInstance> m_Foliage;
        std::optional<ParticleEmitter> m_Particles;
        std::optional<ClothSimulation> m_Cloth;
        std::optional<FluidGrid> m_Fluid;
        std::optional<WorldStreamer> m_Streaming;
        ProductionRuntimeProfile m_Profile;
    };
}
