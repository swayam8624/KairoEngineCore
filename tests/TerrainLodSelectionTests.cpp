#include "../detail/TerrainLodSelection.hpp"
#include <array>
#include <iostream>
#include <random>

namespace {
struct Observer {struct {double x,y,z;} Position;double RadiusScale=1;};
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
int main()
{
    using kairo::engine::detail::SelectTerrainLodStride;
    constexpr std::array thresholds{64.0,128.0,256.0};
    const auto at=[&](double distance,unsigned previous=0u)
    {
        const std::array observers{Observer{{32+distance,0,16},1}};
        return SelectTerrainLodStride(0,0,32,32,observers,thresholds,.1,previous);
    };
    Check(at(0)==1&&at(64)==2&&at(128)==4&&at(256)==8,"Initial bands");
    Check(at(69,1)==1&&at(71,1)==2&&at(60,2)==2&&at(57,2)==1,"Hysteresis boundaries");
    Check(at(1000,1)==8&&at(0,8)==1,"Teleport across levels");
    std::uint32_t stride=1;
    for(unsigned i=0;i<10000;++i)
    {
        stride=at(i%2?65:63,stride);
        Check(stride==1,"Outward threshold chatter");
    }
    stride=2;
    for(unsigned i=0;i<10000;++i)
    {
        stride=at(i%2?65:63,stride);
        Check(stride==2,"Inward threshold chatter");
    }
    std::mt19937 rng(926);
    std::uniform_real_distribution<double> pos(-1000,1000),scale(.5,2);
    for(unsigned i=0;i<10000;++i)
    {
        const std::array observers{Observer{{pos(rng),0,pos(rng)},scale(rng)},
            Observer{{pos(rng),0,pos(rng)},scale(rng)}};
        const auto s=SelectTerrainLodStride(-3,5,32,32,observers,thresholds,.1);
        const auto a=SelectTerrainLodStride(-3,5,32,32,std::span(observers).first(1),thresholds,.1);
        const auto b=SelectTerrainLodStride(-3,5,32,32,std::span(observers).last(1),thresholds,.1);
        Check(s==std::min(a,b),"Nearest scaled observer ignored");
    }
    bool rejected=false;
    try{const std::array o{Observer{{0,0,0},1}};(void)SelectTerrainLodStride(0,0,32,30,o,thresholds,.1);}
    catch(const std::invalid_argument&){rejected=true;}
    Check(rejected,"Nondivisible interval count accepted");
    std::cout<<"20000 threshold oscillations, 10000 multi-observer cases and teleport transitions passed\n";
}
