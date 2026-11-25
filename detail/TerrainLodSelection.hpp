#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace kairo::engine::detail
{
    // Each threshold doubles mesh stride. Distances are nearest horizontal
    // closed-cell AABB distances, divided by observer radius scale.
    // currentStride==0 selects a fresh LOD without hysteresis.
    template<class Observers>
    [[nodiscard]] std::uint32_t SelectTerrainLodStride(
        std::int32_t cellX,std::int32_t cellZ,double extent,
        std::uint32_t intervals,const Observers& observers,
        std::span<const double> thresholds,double hysteresis,
        std::uint32_t currentStride=0)
    {
        if(!std::isfinite(extent)||extent<0.001||extent>1024000.0||
            intervals==0||intervals>1024||
            thresholds.size()>10||!std::isfinite(hysteresis)||
            hysteresis<0||hysteresis>=0.5)
            throw std::invalid_argument("Invalid terrain LOD policy.");
        const auto maxStride=std::uint32_t{1}<<thresholds.size();
        if(intervals%maxStride!=0||
            (currentStride!=0&&((currentStride&(currentStride-1u))!=0||currentStride>maxStride)))
            throw std::invalid_argument("Terrain LOD strides must divide intervals.");
        double previous=0;
        for(double threshold:thresholds)
        {
            if(!std::isfinite(threshold)||threshold<=previous||threshold>1e100)
                throw std::invalid_argument("Terrain LOD distances must be strictly increasing.");
            previous=threshold;
        }
        const double x=cellX*extent,z=cellZ*extent;
        double nearest=std::numeric_limits<double>::infinity();
        for(const auto& observer:observers)
        {
            const double px=observer.Position.x,pz=observer.Position.z;
            const double scale=observer.RadiusScale;
            if(!std::isfinite(px)||!std::isfinite(pz)||
               !std::isfinite(scale)||scale<=0)
                throw std::invalid_argument("Invalid terrain LOD observer.");
            const double dx=std::max({x-px,0.0,px-(x+extent)});
            const double dz=std::max({z-pz,0.0,pz-(z+extent)});
            nearest=std::min(nearest,std::hypot(dx,dz)/scale);
        }
        if(currentStride==0)
        {
            std::uint32_t stride=1;
            for(double threshold:thresholds)
                if(nearest>=threshold)stride*=2u;else break;
            return stride;
        }
        std::size_t level=0;
        for(auto stride=currentStride;stride>1;stride/=2)++level;
        // Crossing outward uses the high boundary; crossing inward uses
        // the low boundary. A teleport may cross several levels at once.
        while(level<thresholds.size()&&nearest>=thresholds[level]*(1+hysteresis))++level;
        while(level>0&&nearest<thresholds[level-1]*(1-hysteresis))--level;
        return std::uint32_t{1}<<level;
    }
}
