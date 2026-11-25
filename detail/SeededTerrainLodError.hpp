#pragma once

#include "SeededTerrainLod.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace kairo::engine::detail
{
    // Work in integer grid units and integer millimeters. The maximum
    // absolute difference of two piecewise-affine height surfaces occurs
    // at a vertex of their planar overlay: a fine-grid vertex, a coarse
    // vertex (also a fine vertex), or a crossing of their edges.
    // All products fit int64: area <= 1024^2, heights are signed int32,
    // and edge-crossing denominators <= 2048.
    struct LodErrorPoint {std::int64_t X,Z,H;};

    [[nodiscard]] inline std::int64_t LodCross(LodErrorPoint a,LodErrorPoint b,
        LodErrorPoint c) noexcept
    {
        return (b.X-a.X)*(c.Z-a.Z)-(b.Z-a.Z)*(c.X-a.X);
    }

    [[nodiscard]] inline std::uint64_t CeilLodError(std::int64_t numerator,
        std::int64_t denominator) noexcept
    {
        const auto magnitude=static_cast<std::uint64_t>(numerator<0?-numerator:numerator);
        const auto divisor=static_cast<std::uint64_t>(denominator);
        return magnitude/divisor+(magnitude%divisor!=0);
    }

    [[nodiscard]] inline std::int64_t FineHeightNumerator(
        const std::vector<std::int32_t>& heights,std::int64_t n,
        std::int64_t x,std::int64_t z,std::int64_t denominator) noexcept
    {
        const auto ix=std::min(x/denominator,n-1),iz=std::min(z/denominator,n-1);
        const auto u=x-ix*denominator,v=z-iz*denominator;
        const auto h=[&](std::int64_t a,std::int64_t b)->std::int64_t
        {return heights[static_cast<std::size_t>(b*(n+1)+a)];};
        if(u+v<=denominator)
            return (denominator-u-v)*h(ix,iz)+u*h(ix+1,iz)+v*h(ix,iz+1);
        return (u+v-denominator)*h(ix+1,iz+1)+(denominator-u)*h(ix,iz+1)+
            (denominator-v)*h(ix+1,iz);
    }

    // Intersect a coarse edge with x=k, z=k and x+z=k, the three
    // families of supporting lines of the canonical fine triangulation.
    // Extra crossings of extended lines are valid surface points, too.
    inline void MeasureLodEdge(LodErrorPoint a,LodErrorPoint b,
        const std::vector<std::int32_t>& heights,std::int64_t n,
        std::uint64_t& error) noexcept
    {
        for(unsigned family=0;family<3;++family)
        {
            const auto line=[&](LodErrorPoint p)
            {return family==0?p.X:family==1?p.Z:p.X+p.Z;};
            auto start=line(a),end=line(b);
            auto first=a,last=b;
            if(start>end){std::swap(start,end);std::swap(first,last);}
            const auto den=end-start;
            if(den==0)continue; // collinear crossings covered by other families
            for(std::int64_t k=1;k<den;++k)
            {
                const auto x=(den-k)*first.X+k*last.X;
                const auto z=(den-k)*first.Z+k*last.Z;
                const auto coarse=(den-k)*first.H+k*last.H;
                error=std::max(error,CeilLodError(
                    coarse-FineHeightNumerator(heights,n,x,z,den),den));
            }
        }
    }

    inline void MeasureLodTriangle(LodErrorPoint a,LodErrorPoint b,LodErrorPoint c,
        const std::vector<std::int32_t>& heights,std::int64_t n,std::uint64_t& error)
    {
        auto area=LodCross(a,b,c);
        if(area<0){std::swap(b,c);area=-area;}
        if(area==0)throw std::logic_error("Degenerate nominal LOD triangle.");
        const auto zmin=std::min({a.Z,b.Z,c.Z}),zmax=std::max({a.Z,b.Z,c.Z});
        const std::array points{a,b,c};
        // Scan only the horizontal interval of each triangle row. Integer
        // floors/ceils give conservative endpoints; barycentric tests
        // reject any extra points. Avoid scanning huge empty bounding boxes.
        for(auto z=zmin;z<=zmax;++z)
        {
            std::int64_t lo=n,hi=0;
            bool intersects=false;
            for(unsigned i=0;i<3;++i)
            {
                auto p=points[i],q=points[(i+1u)%3u];
                if(p.Z>q.Z)std::swap(p,q);
                if(z<p.Z||z>q.Z)continue;
                intersects=true;
                if(p.Z==q.Z)
                {
                    lo=std::min({lo,p.X,q.X});hi=std::max({hi,p.X,q.X});
                }
                else
                {
                    const auto den=q.Z-p.Z;
                    const auto num=(q.Z-z)*p.X+(z-p.Z)*q.X;
                    lo=std::min(lo,num/den);
                    hi=std::max(hi,(num+den-1)/den);
                }
            }
            if(!intersects)continue;
            for(auto x=lo;x<=hi;++x)
            {
                const LodErrorPoint p{x,z,0};
                const auto wa=LodCross(b,c,p),wb=LodCross(c,a,p),wc=LodCross(a,b,p);
                if(wa<0||wb<0||wc<0)continue;
                const auto fine=static_cast<std::int64_t>(
                    heights[static_cast<std::size_t>(z*(n+1)+x)]);
                error=std::max(error,CeilLodError(wa*a.H+wb*b.H+wc*c.H-area*fine,area));
            }
        }
        MeasureLodEdge(a,b,heights,n,error);
        MeasureLodEdge(b,c,heights,n,error);
        MeasureLodEdge(c,a,heights,n,error);
    }

    struct LodErrorVertex
    {
        std::array<float,3> Position{},Normal{};
        std::array<float,2> TexCoord{};
    };
    struct LodErrorMesh
    {
        std::vector<LodErrorVertex> Vertices;
        std::vector<std::uint32_t> Indices;
        bool HasNormals=false,HasTexCoords=false;
    };

    // Exact ceiling in integer millimeters for the NOMINAL surfaces,
    // not a bound on GPU float quantization, shading or screen-space error.
    [[nodiscard]] inline std::uint64_t MeasureSeededTerrainLodError(
        std::uint64_t seed,std::int32_t x,std::int32_t z,
        const SeededHeightConfig& config,std::uint32_t stride)
    {
        config.Validate();
        if(stride==0||(stride&(stride-1u))!=0||config.Intervals%stride!=0)
            throw std::invalid_argument("Invalid terrain LOD stride.");
        if(stride==1)return 0;
        const auto n=static_cast<std::int64_t>(config.Intervals);
        const auto heights=GenerateSeededHeightCell(seed,x,z,config);
        // Unit-meter spacing preserves all grid X/Z positions exactly in
        // float. Read integer heights from the source, never float Y.
        const auto mesh=GenerateSeededTerrainLod<LodErrorMesh>(seed,x,z,config,1000,stride);
        const auto point=[&](std::uint32_t id)
        {
            const auto& v=mesh.Vertices[id];
            const auto px=static_cast<std::int64_t>(v.Position[0]);
            const auto pz=static_cast<std::int64_t>(v.Position[2]);
            return LodErrorPoint{px,pz,heights[static_cast<std::size_t>(pz*(n+1)+px)]};
        };
        std::uint64_t error=0;
        for(std::size_t i=0;i<mesh.Indices.size();i+=3)
            MeasureLodTriangle(point(mesh.Indices[i]),point(mesh.Indices[i+1]),
                point(mesh.Indices[i+2]),heights,n,error);
        return error;
    }
}
