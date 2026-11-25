#pragma once

#include "SeededHeightfield.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace kairo::engine::detail
{
    struct TerrainSurface
    {
        double HeightMeters;
        std::array<double,3> Normal;
    };

    // Sample the piecewise planar surface of the mesh's fixed diagonal,
    // rather than the bilinear noise function between grid vertices.
    // Query coordinates are cell-local meters. No allocations or mesh build.
    [[nodiscard]] inline std::optional<TerrainSurface> QuerySeededTerrainSurface(
        std::uint64_t seed,std::int32_t cellX,std::int32_t cellZ,
        const SeededHeightConfig& config,std::uint32_t spacingMillimeters,
        double localX,double localZ)
    {
        config.Validate();
        if (spacingMillimeters==0 || spacingMillimeters>1000000)
            throw std::invalid_argument("Terrain sample spacing must be 1..1000000 millimeters.");
        if (!std::isfinite(localX)||!std::isfinite(localZ))
            throw std::invalid_argument("Terrain query must be finite.");
        const double step=static_cast<double>(spacingMillimeters)/1000.0;
        const double extent=static_cast<double>(config.Intervals)*spacingMillimeters/1000.0;
        if(localX<0 || localZ<0 || localX>extent || localZ>extent) return std::nullopt;
        const auto ix=std::min(static_cast<std::uint32_t>(localX/step),config.Intervals-1u);
        const auto iz=std::min(static_cast<std::uint32_t>(localZ/step),config.Intervals-1u);
        const double u=std::clamp(localX/step-ix,0.0,1.0);
        const double v=std::clamp(localZ/step-iz,0.0,1.0);
        const auto gx=static_cast<std::int64_t>(cellX)*config.Intervals+ix;
        const auto gz=static_cast<std::int64_t>(cellZ)*config.Intervals+iz;
        const auto h=[&](std::int64_t x,std::int64_t z)
        {return static_cast<double>(SampleSeededHeight(seed,x,z,config))/1000.0;};
        const double a=h(gx,gz), b=h(gx+1,gz), c=h(gx,gz+1), d=h(gx+1,gz+1);
        double height,dx,dz;
        if(u+v<=1.0)
        {
            dx=b-a;dz=c-a;
            height=a+u*dx+v*dz;
        }
        else
        {
            dx=d-c;dz=d-b;
            height=d-(1.0-u)*dx-(1.0-v)*dz;
        }
        // Face normal (not interpolated shading normal), for grounding.
        const double length=std::sqrt(dx*dx+step*step+dz*dz);
        return TerrainSurface{height,{-dx/length,step/length,-dz/length}};
    }
}
