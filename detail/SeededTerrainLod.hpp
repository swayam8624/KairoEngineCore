#pragma once

#include "SeededTerrainMesh.hpp"
#include <array>
#include <limits>
#include <unordered_map>

namespace kairo::engine::detail
{
    // Fixed full-resolution perimeter. Coarse interior quads use the
    // regular diagonal; perimeter quads fan around a sampled center.
    // No neighbor LOD knowledge, skirts, or post-generation stitching.
    template<class Mesh>
    [[nodiscard]] Mesh GenerateSeededTerrainLod(std::uint64_t seed,
        std::int32_t cellX,std::int32_t cellZ,const SeededHeightConfig& config,
        std::uint32_t spacing,std::uint32_t stride)
    {
        config.Validate();
        if(spacing==0||spacing>1000000u||stride==0||
            (stride&(stride-1u))!=0||config.Intervals%stride!=0)
            throw std::invalid_argument("Terrain LOD stride must be a power of two dividing intervals; spacing must be valid.");
        if(stride==1)return GenerateSeededTerrainMesh<Mesh>(seed,cellX,cellZ,config,spacing);
        const auto n=config.Intervals;
        const auto ox=static_cast<std::int64_t>(cellX)*n;
        const auto oz=static_cast<std::int64_t>(cellZ)*n;
        Mesh mesh;
        mesh.HasNormals=true;mesh.HasTexCoords=true;
        std::unordered_map<std::uint32_t,std::uint32_t> ids;
        const auto coarse=n/stride;
        const auto estimate=(coarse+1u)*(coarse+1u)+4u*n+4u*coarse;
        ids.reserve(estimate);
        mesh.Vertices.reserve(estimate);
        const auto sample=[&](std::int64_t x,std::int64_t z)
        {return SampleSeededHeight(seed,ox+x,oz+z,config);};
        const auto vertex=[&](std::uint32_t x,std::uint32_t z)
        {
            const auto key=z*(n+1u)+x;
            if(const auto it=ids.find(key);it!=ids.end())return it->second;
            const auto id=static_cast<std::uint32_t>(mesh.Vertices.size());
            mesh.Vertices.emplace_back();
            auto& v=mesh.Vertices.back();
            const auto sx=static_cast<std::int64_t>(x),sz=static_cast<std::int64_t>(z);
            const double dx=static_cast<double>(sample(sx-1,sz))-sample(sx+1,sz);
            const double dz=static_cast<double>(sample(sx,sz-1))-sample(sx,sz+1);
            const double dy=2.0*spacing;
            const double length=std::sqrt(dx*dx+dy*dy+dz*dz);
            v.Position={static_cast<float>(static_cast<double>(x)*spacing/1000.0),
                static_cast<float>(static_cast<double>(sample(sx,sz))/1000.0),
                static_cast<float>(static_cast<double>(z)*spacing/1000.0)};
            v.Normal={static_cast<float>(dx/length),static_cast<float>(dy/length),static_cast<float>(dz/length)};
            v.TexCoord={static_cast<float>(x)/static_cast<float>(n),static_cast<float>(z)/static_cast<float>(n)};
            ids.emplace(key,id);
            return id;
        };
        // Reused scratch: at most four full-resolution edges of a quad.
        std::vector<std::uint32_t> ring;
        ring.reserve(4u*stride);
        for(std::uint32_t z=0;z<n;z+=stride)
            for(std::uint32_t x=0;x<n;x+=stride)
            {
                const auto right=x+stride,bottom=z+stride;
                const auto a=vertex(x,z),b=vertex(right,z),c=vertex(x,bottom),d=vertex(right,bottom);
                if(x!=0&&z!=0&&right!=n&&bottom!=n)
                {
                    mesh.Indices.insert(mesh.Indices.end(),{a,c,b,b,c,d});
                    continue;
                }
                ring.clear();
                // Upward winding: left, bottom, right, top. Include each
                // corner once; subdivide only the actual outer cell edge.
                for(auto k=0u;k<stride;k+=(x==0?1u:stride))
                    ring.push_back(vertex(x,z+k));
                for(auto k=0u;k<stride;k+=(bottom==n?1u:stride))
                    ring.push_back(vertex(x+k,bottom));
                for(auto k=0u;k<stride;k+=(right==n?1u:stride))
                    ring.push_back(vertex(right,bottom-k));
                for(auto k=0u;k<stride;k+=(z==0?1u:stride))
                    ring.push_back(vertex(right-k,z));
                const auto center=vertex(x+stride/2u,z+stride/2u);
                for(std::size_t i=0;i<ring.size();++i)
                    mesh.Indices.insert(mesh.Indices.end(),{center,ring[i],ring[(i+1u)%ring.size()]});
            }
        return mesh;
    }
}
