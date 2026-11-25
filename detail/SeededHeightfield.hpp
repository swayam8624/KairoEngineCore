#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace kairo::engine::detail
{
    // Version 1: integer-only coordinate hashing and interpolation.
    // Do not change these constants or rounding without a generator version
    // change: saved worlds depend on reproducible values across hosts.
    [[nodiscard]] constexpr std::uint64_t MixWorldBits(std::uint64_t x) noexcept
    {
        x += 0x9e3779b97f4a7c15ull;
        x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
        return x ^ (x >> 31u);
    }

    [[nodiscard]] constexpr std::uint64_t WorldCoordinateBits(
        std::uint64_t seed, std::int64_t x, std::int64_t z,
        std::uint64_t domain) noexcept
    {
        // Conversion of signed coordinates to uint64 is defined modulo 2^64.
        // No std::hash, global RNG state, endian-sensitive byte serialization
        // or floating-point distribution is involved.
        return MixWorldBits(MixWorldBits(seed ^ domain) ^
            MixWorldBits(static_cast<std::uint64_t>(x) ^ 0x243f6a8885a308d3ull) ^
            MixWorldBits(static_cast<std::uint64_t>(z) ^ 0x13198a2e03707344ull));
    }

    struct SeededHeightConfig
    {
        std::uint32_t Intervals = 32u;
        std::uint32_t LatticeSpacing = 16u;
        std::int32_t Minimum = -10000;
        std::int32_t Maximum = 30000;

        void Validate() const
        {
            // Each cell contains (Intervals+1)^2 samples including borders.
            // Cap output allocation and interpolation products explicitly.
            if (Intervals == 0u || Intervals > 1024u ||
                LatticeSpacing == 0u || LatticeSpacing > 65536u ||
                Minimum > Maximum)
                throw std::invalid_argument("Invalid seeded heightfield configuration.");
        }
    };

    struct FloorLatticeCoordinate
    {
        std::int64_t Quotient;
        std::uint32_t Remainder;
    };

    // C++ integer division truncates toward zero. Negative-world lattice
    // coordinates need mathematical floor to maintain shared cell borders.
    [[nodiscard]] constexpr FloorLatticeCoordinate FloorLattice(
        std::int64_t coordinate,std::uint32_t spacing) noexcept
    {
        const auto s=static_cast<std::int64_t>(spacing);
        auto q=coordinate/s, r=coordinate%s;
        if(r<0) { --q; r+=s; }
        return {q,static_cast<std::uint32_t>(r)};
    }

    [[nodiscard]] constexpr std::int64_t LatticeHeight(
        std::uint64_t seed,std::int64_t x,std::int64_t z,
        const SeededHeightConfig& config) noexcept
    {
        const auto span=static_cast<std::uint64_t>(
            static_cast<std::int64_t>(config.Maximum)-config.Minimum)+1u;
        const auto bits=WorldCoordinateBits(seed,x,z,0x4845494748543031ull);
        return static_cast<std::int64_t>(config.Minimum)+
            static_cast<std::int64_t>(bits%span);
    }

    [[nodiscard]] constexpr std::int64_t LerpHeight(std::int64_t a,std::int64_t b,
        std::uint32_t remainder,std::uint32_t spacing) noexcept
    {
        // Products are <= (2^32-1)*65535; signed int64 is sufficient.
        // Division rounding is specified by C++, including negative deltas.
        return a+(b-a)*static_cast<std::int64_t>(remainder)/spacing;
    }

    [[nodiscard]] constexpr std::int32_t SampleSeededHeight(
        std::uint64_t seed,std::int64_t x,std::int64_t z,
        const SeededHeightConfig& config) noexcept
    {
        // Internal precondition: validated config and coordinates derived
        // from int32 cell indices * <=1024 intervals. q+1 cannot overflow.
        const auto a=FloorLattice(x,config.LatticeSpacing);
        const auto b=FloorLattice(z,config.LatticeSpacing);
        const auto lo=LerpHeight(LatticeHeight(seed,a.Quotient,b.Quotient,config),
            LatticeHeight(seed,a.Quotient+1,b.Quotient,config),a.Remainder,config.LatticeSpacing);
        const auto hi=LerpHeight(LatticeHeight(seed,a.Quotient,b.Quotient+1,config),
            LatticeHeight(seed,a.Quotient+1,b.Quotient+1,config),a.Remainder,config.LatticeSpacing);
        return static_cast<std::int32_t>(LerpHeight(lo,hi,b.Remainder,config.LatticeSpacing));
    }

    [[nodiscard]] inline std::vector<std::int32_t> GenerateSeededHeightCell(
        std::uint64_t seed,std::int32_t cellX,std::int32_t cellZ,
        const SeededHeightConfig& config)
    {
        config.Validate();
        const auto side=static_cast<std::size_t>(config.Intervals)+1u;
        std::vector<std::int32_t> heights(side*side);
        const auto originX=static_cast<std::int64_t>(cellX)*config.Intervals;
        const auto originZ=static_cast<std::int64_t>(cellZ)*config.Intervals;
        for(std::uint32_t z=0;z<=config.Intervals;++z)
            for(std::uint32_t x=0;x<=config.Intervals;++x)
                heights[static_cast<std::size_t>(z)*side+x]=
                    SampleSeededHeight(seed,originX+x,originZ+z,config);
        return heights;
    }
}
