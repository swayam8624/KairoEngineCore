#pragma once

#include "SeededHeightfield.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace kairo::engine::detail
{
    // Mesh is a structural type with Vertices, Indices, HasNormals and
    // HasTexCoords; the public module supplies the canonical MeshArtifactData.
    // A one-sample halo is generated from GLOBAL coordinates, not clamped
    // local borders. Neighboring cells therefore derive identical normals.
    template<class Mesh>
    [[nodiscard]] Mesh GenerateSeededTerrainMesh(std::uint64_t seed,
        std::int32_t cellX,std::int32_t cellZ,const SeededHeightConfig& config,
        std::uint32_t sampleSpacingMillimeters)
    {
        config.Validate();
        if (sampleSpacingMillimeters == 0u || sampleSpacingMillimeters > 1000000u)
            throw std::invalid_argument("Terrain sample spacing must be 1..1000000 millimeters.");
        const auto n=static_cast<std::size_t>(config.Intervals);
        const auto side=n+1u, haloSide=n+3u;
        const auto originX=static_cast<std::int64_t>(cellX)*config.Intervals;
        const auto originZ=static_cast<std::int64_t>(cellZ)*config.Intervals;
        std::vector<std::int32_t> halo(haloSide*haloSide);
        for(std::size_t z=0;z<haloSide;++z)
            for(std::size_t x=0;x<haloSide;++x)
                halo[z*haloSide+x]=SampleSeededHeight(seed,
                    originX+static_cast<std::int64_t>(x)-1,
                    originZ+static_cast<std::int64_t>(z)-1,config);
        Mesh mesh;
        mesh.HasNormals=true;
        mesh.HasTexCoords=true;
        mesh.Vertices.resize(side*side);
        mesh.Indices.reserve(6u*n*n);
        for(std::size_t z=0;z<side;++z)
            for(std::size_t x=0;x<side;++x)
            {
                const auto center=(z+1u)*haloSide+x+1u;
                // Central difference in millimeters; double avoids overflow
                // in signed 32-bit height subtraction and normalization.
                const double dx=static_cast<double>(halo[center-1])-halo[center+1];
                const double dz=static_cast<double>(halo[center-haloSide])-halo[center+haloSide];
                const double dy=2.0*sampleSpacingMillimeters;
                const double length=std::sqrt(dx*dx+dy*dy+dz*dz);
                auto& v=mesh.Vertices[z*side+x];
                v.Position={static_cast<float>(static_cast<double>(x)*sampleSpacingMillimeters/1000.0),
                    static_cast<float>(static_cast<double>(halo[center])/1000.0),
                    static_cast<float>(static_cast<double>(z)*sampleSpacingMillimeters/1000.0)};
                v.Normal={static_cast<float>(dx/length),static_cast<float>(dy/length),
                    static_cast<float>(dz/length)};
                // Tile-local UVs; repeat-wrapped textures join at 0/1.
                v.TexCoord={static_cast<float>(x)/static_cast<float>(n),
                    static_cast<float>(z)/static_cast<float>(n)};
            }
        for(std::size_t z=0;z<n;++z)
            for(std::size_t x=0;x<n;++x)
            {
                const auto a=static_cast<std::uint32_t>(z*side+x);
                const auto b=a+1u,c=a+static_cast<std::uint32_t>(side),d=c+1u;
                // X right, Y up, Z forward: CCW triangles viewed from above.
                mesh.Indices.insert(mesh.Indices.end(),{a,c,b,b,c,d});
            }
        return mesh;
    }
}
