#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace kairo::engine::detail
{
    // The scan budget bounds enumeration work even for duplicate/overlapping
    // observers. Candidate generation is pure: no residency state is mutated
    // if an observer is invalid or asks for an excessive region.
    template<class Observers>
    [[nodiscard]] std::set<std::pair<std::int32_t,std::int32_t>>
    DiscoverProceduralCells(double cellSize,double loadRadius,
        const Observers& observers,std::size_t maximumScannedCells)
    {
        if (!std::isfinite(cellSize)||cellSize<=0 ||
            !std::isfinite(loadRadius)||loadRadius<0 || maximumScannedCells==0)
            throw std::invalid_argument("Invalid procedural cell discovery limits.");
        using Coordinate=std::pair<std::int32_t,std::int32_t>;
        constexpr double minimum=std::numeric_limits<std::int32_t>::min();
        constexpr double maximum=std::numeric_limits<std::int32_t>::max();
        std::set<Coordinate> result;
        std::size_t remaining=maximumScannedCells;
        for(const auto& observer:observers)
        {
            const double radius=loadRadius*observer.RadiusScale;
            const double x=observer.Position.x,z=observer.Position.z;
            if(!std::isfinite(x)||!std::isfinite(z)||
                !std::isfinite(observer.RadiusScale)||observer.RadiusScale<=0 ||
                !std::isfinite(radius))
                throw std::invalid_argument("Invalid procedural observer.");
            // Expand by one grid index to include touching cells at exact
            // boundaries (closed AABB), and guard quotient rounding.
            const double lx=std::floor((x-radius)/cellSize)-1.0;
            const double hx=std::floor((x+radius)/cellSize)+1.0;
            const double lz=std::floor((z-radius)/cellSize)-1.0;
            const double hz=std::floor((z+radius)/cellSize)+1.0;
            if(!std::isfinite(lx)||!std::isfinite(hx)||
               !std::isfinite(lz)||!std::isfinite(hz))
                throw std::length_error("Procedural observer exceeds coordinate range.");
            if(hx<minimum||lx>maximum||hz<minimum||lz>maximum) continue;
            const auto x0=static_cast<std::int64_t>(std::max(lx,minimum));
            const auto x1=static_cast<std::int64_t>(std::min(hx,maximum));
            const auto z0=static_cast<std::int64_t>(std::max(lz,minimum));
            const auto z1=static_cast<std::int64_t>(std::min(hz,maximum));
            const auto width=static_cast<std::uint64_t>(x1-x0)+1u;
            const auto depth=static_cast<std::uint64_t>(z1-z0)+1u;
            if(width>remaining || depth>remaining/width)
                throw std::length_error("Procedural cell discovery scan budget exceeded.");
            remaining-=static_cast<std::size_t>(width*depth);
            for(auto cx=x0;cx<=x1;++cx)
                for(auto cz=z0;cz<=z1;++cz)
                {
                    const double minX=static_cast<double>(cx)*cellSize;
                    const double minZ=static_cast<double>(cz)*cellSize;
                    const double maxX=minX+cellSize,maxZ=minZ+cellSize;
                    const double dx=x<minX?minX-x:x>maxX?x-maxX:0.0;
                    const double dz=z<minZ?minZ-z:z>maxZ?z-maxZ:0.0;
                    // Same closed-AABB distance criterion as residency
                    // planner. Restrict numerical domain at the public
                    // wrapper to avoid squared-distance overflow.
                    if(dx*dx+dz*dz<=radius*radius)
                        result.emplace(static_cast<std::int32_t>(cx),static_cast<std::int32_t>(cz));
                }
        }
        return result;
    }
}
