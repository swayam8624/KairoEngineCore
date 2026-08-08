module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.EngineCore.ProductionSystems;

export namespace kairo::engine
{
    struct ProductionVec3 final { double X = 0.0, Y = 0.0, Z = 0.0; };

    struct AnimationKeyframe final
    {
        double Time = 0.0;
        ProductionVec3 Translation{};
    };

    class AnimationClip final
    {
    public:
        explicit AnimationClip(double duration = 0.0) : m_Duration(duration)
        {
            if (!std::isfinite(duration) || duration < 0.0)
                throw std::invalid_argument("Animation duration must be finite and non-negative.");
        }

        void AddKey(std::string channel, AnimationKeyframe key)
        {
            if (channel.empty() || !std::isfinite(key.Time) || key.Time < 0.0 || key.Time > m_Duration || !Finite(key.Translation))
                throw std::invalid_argument("Animation key is invalid.");
            auto& keys = m_Channels[std::move(channel)];
            keys.push_back(key);
            std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
        }

        [[nodiscard]] ProductionVec3 Sample(std::string_view channel, double time, bool loop = true) const
        {
            const auto found = m_Channels.find(std::string(channel));
            if (found == m_Channels.end() || found->second.empty()) throw std::out_of_range("Animation channel has no keys.");
            if (!std::isfinite(time)) throw std::invalid_argument("Animation sample time must be finite.");
            if (m_Duration > 0.0 && loop)
            {
                time = std::fmod(time, m_Duration);
                if (time < 0.0) time += m_Duration;
            }
            const auto& keys = found->second;
            if (time <= keys.front().Time) return keys.front().Translation;
            if (time >= keys.back().Time) return keys.back().Translation;
            const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                [](double value, const AnimationKeyframe& key) { return value < key.Time; });
            const auto& b = *upper;
            const auto& a = *(upper - 1);
            const double t = (time - a.Time) / (b.Time - a.Time);
            return Lerp(a.Translation, b.Translation, t);
        }

        [[nodiscard]] double Duration() const noexcept { return m_Duration; }

    private:
        double m_Duration = 0.0;
        std::map<std::string, std::vector<AnimationKeyframe>> m_Channels;
        static bool Finite(ProductionVec3 value) noexcept
        { return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z); }
        static ProductionVec3 Lerp(ProductionVec3 a, ProductionVec3 b, double t) noexcept
        { return { a.X + (b.X-a.X)*t, a.Y + (b.Y-a.Y)*t, a.Z + (b.Z-a.Z)*t }; }
    };

    class TerrainHeightfield final
    {
    public:
        TerrainHeightfield(std::size_t width, std::size_t height, double cellSize = 1.0)
            : m_Width(width), m_Height(height), m_CellSize(cellSize), m_Heights(width * height, 0.0)
        {
            if (width < 2u || height < 2u || !std::isfinite(cellSize) || cellSize <= 0.0)
                throw std::invalid_argument("Terrain dimensions or cell size are invalid.");
            if (width > 16384u || height > 16384u) throw std::length_error("Terrain exceeds safety dimensions.");
        }
        [[nodiscard]] std::size_t Width() const noexcept { return m_Width; }
        [[nodiscard]] std::size_t Height() const noexcept { return m_Height; }
        [[nodiscard]] double CellSize() const noexcept { return m_CellSize; }
        [[nodiscard]] double HeightAt(std::size_t x, std::size_t z) const { return m_Heights.at(z*m_Width+x); }
        void SetHeight(std::size_t x, std::size_t z, double value)
        {
            if (!std::isfinite(value)) throw std::invalid_argument("Terrain height must be finite.");
            m_Heights.at(z*m_Width+x) = value;
        }
        void Sculpt(double worldX, double worldZ, double radius, double amount)
        {
            if (!std::isfinite(worldX) || !std::isfinite(worldZ) || !std::isfinite(radius) || radius <= 0.0 || !std::isfinite(amount))
                throw std::invalid_argument("Terrain sculpt parameters are invalid.");
            for (std::size_t z=0; z<m_Height; ++z) for (std::size_t x=0; x<m_Width; ++x)
            {
                const double dx = static_cast<double>(x)*m_CellSize-worldX;
                const double dz = static_cast<double>(z)*m_CellSize-worldZ;
                const double d = std::sqrt(dx*dx+dz*dz);
                if (d < radius) m_Heights[z*m_Width+x] += amount * (1.0-d/radius);
            }
        }
    private:
        std::size_t m_Width=0, m_Height=0;
        double m_CellSize=1.0;
        std::vector<double> m_Heights;
    };

    struct FoliageInstance final
    {
        ProductionVec3 Position{};
        double Scale = 1.0;
        double RotationY = 0.0;
    };

    [[nodiscard]] inline std::vector<FoliageInstance> ScatterFoliage(
        const TerrainHeightfield& terrain, std::size_t count, std::uint64_t seed)
    {
        std::vector<FoliageInstance> result;
        result.reserve(count);
        auto next = [&seed]() {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<double>((seed >> 11u) & ((1ULL<<53u)-1u)) / static_cast<double>(1ULL<<53u);
        };
        for (std::size_t i=0; i<count; ++i)
        {
            const double fx = next() * static_cast<double>(terrain.Width()-1u);
            const double fz = next() * static_cast<double>(terrain.Height()-1u);
            const std::size_t x = std::min(static_cast<std::size_t>(fx), terrain.Width()-1u);
            const std::size_t z = std::min(static_cast<std::size_t>(fz), terrain.Height()-1u);
            result.push_back({ { fx*terrain.CellSize(), terrain.HeightAt(x,z), fz*terrain.CellSize() },
                0.75 + next()*0.5, next()*6.283185307179586 });
        }
        return result;
    }

    struct Particle final
    {
        ProductionVec3 Position{};
        ProductionVec3 Velocity{};
        double RemainingLife = 0.0;
    };

    class ParticleEmitter final
    {
    public:
        explicit ParticleEmitter(std::size_t capacity = 4096u) : m_Capacity(capacity)
        { if (capacity == 0u) throw std::invalid_argument("Particle capacity cannot be zero."); }
        void Emit(Particle particle)
        {
            if (!std::isfinite(particle.RemainingLife) || particle.RemainingLife <= 0.0)
                throw std::invalid_argument("Particle lifetime must be positive and finite.");
            if (m_Particles.size() >= m_Capacity) m_Particles.erase(m_Particles.begin());
            m_Particles.push_back(particle);
        }
        void Update(double dt)
        {
            if (!std::isfinite(dt) || dt < 0.0) throw std::invalid_argument("Particle delta is invalid.");
            for (auto& p : m_Particles)
            {
                p.Position.X += p.Velocity.X*dt; p.Position.Y += p.Velocity.Y*dt; p.Position.Z += p.Velocity.Z*dt;
                p.RemainingLife -= dt;
            }
            std::erase_if(m_Particles, [](const Particle& p) { return p.RemainingLife <= 0.0; });
        }
        [[nodiscard]] const std::vector<Particle>& Particles() const noexcept { return m_Particles; }
    private:
        std::size_t m_Capacity;
        std::vector<Particle> m_Particles;
    };

    struct ClothParticle final
    {
        ProductionVec3 Position{};
        ProductionVec3 Previous{};
        bool Pinned = false;
    };

    struct ClothConstraint final { std::size_t A=0u, B=0u; double RestLength=0.0; };

    class ClothSimulation final
    {
    public:
        std::size_t AddParticle(ClothParticle p) { m_Particles.push_back(p); return m_Particles.size()-1u; }
        void AddConstraint(std::size_t a, std::size_t b)
        {
            if (a>=m_Particles.size() || b>=m_Particles.size() || a==b) throw std::out_of_range("Cloth constraint indices are invalid.");
            m_Constraints.push_back({a,b,Distance(m_Particles[a].Position,m_Particles[b].Position)});
        }
        void Step(double dt, ProductionVec3 acceleration = {0.0,-9.81,0.0}, std::size_t iterations=4u)
        {
            if (!std::isfinite(dt) || dt<=0.0 || iterations==0u) throw std::invalid_argument("Cloth step parameters are invalid.");
            for (auto& p:m_Particles) if (!p.Pinned)
            {
                const ProductionVec3 current=p.Position;
                p.Position.X += p.Position.X-p.Previous.X + acceleration.X*dt*dt;
                p.Position.Y += p.Position.Y-p.Previous.Y + acceleration.Y*dt*dt;
                p.Position.Z += p.Position.Z-p.Previous.Z + acceleration.Z*dt*dt;
                p.Previous=current;
            }
            for (std::size_t iteration=0; iteration<iterations; ++iteration)
                for (const auto& c:m_Constraints)
                {
                    auto& a=m_Particles[c.A]; auto& b=m_Particles[c.B];
                    const double d=Distance(a.Position,b.Position); if (d<=1.0e-12) continue;
                    const double correction=(d-c.RestLength)/d*0.5;
                    const ProductionVec3 delta{(b.Position.X-a.Position.X)*correction,
                        (b.Position.Y-a.Position.Y)*correction,(b.Position.Z-a.Position.Z)*correction};
                    if (!a.Pinned) { a.Position.X+=delta.X; a.Position.Y+=delta.Y; a.Position.Z+=delta.Z; }
                    if (!b.Pinned) { b.Position.X-=delta.X; b.Position.Y-=delta.Y; b.Position.Z-=delta.Z; }
                }
        }
        [[nodiscard]] const std::vector<ClothParticle>& Particles() const noexcept { return m_Particles; }
    private:
        std::vector<ClothParticle> m_Particles;
        std::vector<ClothConstraint> m_Constraints;
        static double Distance(ProductionVec3 a, ProductionVec3 b) noexcept
        { const double x=a.X-b.X,y=a.Y-b.Y,z=a.Z-b.Z; return std::sqrt(x*x+y*y+z*z); }
    };

    class FluidGrid final
    {
    public:
        FluidGrid(std::size_t width, std::size_t height) : m_Width(width),m_Height(height),m_Density(width*height,0.0)
        { if(width<3u||height<3u) throw std::invalid_argument("Fluid grid must be at least 3x3."); }
        void AddDensity(std::size_t x,std::size_t y,double amount)
        { if(!std::isfinite(amount)) throw std::invalid_argument("Fluid density must be finite."); m_Density.at(y*m_Width+x)+=amount; }
        void Diffuse(double factor)
        {
            if(!std::isfinite(factor)||factor<0.0||factor>0.25) throw std::invalid_argument("Fluid diffusion factor must be in [0,0.25].");
            auto next=m_Density;
            for(std::size_t y=1;y+1<m_Height;++y) for(std::size_t x=1;x+1<m_Width;++x)
            {
                const auto i=y*m_Width+x;
                next[i]=m_Density[i]*(1.0-4.0*factor)+factor*(m_Density[i-1]+m_Density[i+1]+m_Density[i-m_Width]+m_Density[i+m_Width]);
            }
            m_Density.swap(next);
        }
        [[nodiscard]] double Density(std::size_t x,std::size_t y) const { return m_Density.at(y*m_Width+x); }
    private:
        std::size_t m_Width=0,m_Height=0;
        std::vector<double> m_Density;
    };

    struct WorldCell final
    {
        std::int32_t X=0, Z=0;
        friend constexpr auto operator<=>(const WorldCell&, const WorldCell&) noexcept = default;
    };

    class WorldStreamer final
    {
    public:
        explicit WorldStreamer(double cellSize=128.0,std::int32_t radius=2) : m_CellSize(cellSize),m_Radius(radius)
        {
            if(!std::isfinite(cellSize)||cellSize<=0.0||radius<0||radius>64) throw std::invalid_argument("World streaming configuration is invalid.");
        }
        void Update(double worldX,double worldZ)
        {
            if(!std::isfinite(worldX)||!std::isfinite(worldZ)) throw std::invalid_argument("Streaming origin must be finite.");
            const auto cx=static_cast<std::int32_t>(std::floor(worldX/m_CellSize));
            const auto cz=static_cast<std::int32_t>(std::floor(worldZ/m_CellSize));
            std::set<WorldCell> desired;
            for(std::int32_t z=-m_Radius;z<=m_Radius;++z) for(std::int32_t x=-m_Radius;x<=m_Radius;++x)
                desired.insert({cx+x,cz+z});
            m_Loaded.swap(desired);
        }
        [[nodiscard]] const std::set<WorldCell>& Loaded() const noexcept { return m_Loaded; }
    private:
        double m_CellSize=128.0;
        std::int32_t m_Radius=2;
        std::set<WorldCell> m_Loaded;
    };
}
