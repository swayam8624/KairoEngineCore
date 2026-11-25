#include "../detail/ProceduralCellDiscovery.hpp"
#include <array>
#include <iostream>
#include <random>
#include <vector>

namespace {
struct Observer {struct {double x,y,z;} Position;double RadiusScale=1;};
void Check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
}
int main()
{
    using namespace kairo::engine::detail;
    const std::array boundary{Observer{{0,0,0},1}};
    const auto touching=DiscoverProceduralCells(32,0,boundary,100);
    Check(touching.size()==4 && touching.contains({-1,-1}) &&
        touching.contains({0,0}),"Closed cell corner discovery differs");
    std::mt19937 rng(72527);
    std::uniform_real_distribution<double> pos(-120,120),scale(.1,2);
    std::size_t tested=0;
    for(unsigned trial=0;trial<1000;++trial)
    {
        const std::array observers{Observer{{pos(rng),0,pos(rng)},scale(rng)},
            Observer{{pos(rng),0,pos(rng)},scale(rng)}};
        const auto actual=DiscoverProceduralCells(16,32,observers,4096);
        std::set<std::pair<std::int32_t,std::int32_t>> oracle;
        for(int x=-16;x<=16;++x)
            for(int z=-16;z<=16;++z)
                for(const auto& o:observers)
                {
                    const double dx=std::max({x*16.0-o.Position.x,0.0,o.Position.x-(x+1)*16.0});
                    const double dz=std::max({z*16.0-o.Position.z,0.0,o.Position.z-(z+1)*16.0});
                    const double radius=32*o.RadiusScale;
                    if(dx*dx+dz*dz<=radius*radius)oracle.emplace(x,z);
                    ++tested;
                }
        Check(actual==oracle,"Discovery differs from brute force");
        const std::array reversed{observers[1],observers[0]};
        Check(DiscoverProceduralCells(16,32,reversed,4096)==actual,"Observer order changed result");
    }
    const std::array edge{Observer{{2147483647.0,0,-2147483648.0},1}};
    Check(DiscoverProceduralCells(1,0,edge,100).size()==2,"Signed boundary clipping incorrect");
    bool rejected=false;
    try{(void)DiscoverProceduralCells(1,1000,boundary,100);}
    catch(const std::length_error&){rejected=true;}
    Check(rejected,"Scan budget ignored");
    const std::array<Observer,0> empty{};
    Check(DiscoverProceduralCells(16,32,empty,100).empty(),"No observers discovered cells");
    std::cout<<tested<<" brute-force observer/cell comparisons, closed borders, order and scan cap passed\n";
}
