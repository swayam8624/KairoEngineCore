#include "../detail/SeededHeightfield.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

int main()
{
    using namespace kairo::engine::detail;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    static_assert(WorldCoordinateBits(0,0,0,0)==0xe92dfd851821f63full);
    static_assert(WorldCoordinateBits(42,-1,2,0)==0xedccb03e681c4045ull);
    static_assert(WorldCoordinateBits(42,-1,2,1)==0x125927930b943431ull);
    SeededHeightConfig config;
    config.Intervals=31;
    config.LatticeSpacing=7;
    std::size_t borders=0;
    for(std::uint64_t seed: {0ull,42ull,999999ull})
        for(std::int32_t z=-8;z<8;++z)
            for(std::int32_t x=-8;x<8;++x)
            {
                const auto cell=GenerateSeededHeightCell(seed,x,z,config);
                const auto east=GenerateSeededHeightCell(seed,x+1,z,config);
                const auto north=GenerateSeededHeightCell(seed,x,z+1,config);
                const std::size_t side=config.Intervals+1u;
                for(std::size_t i=0;i<side;++i)
                {
                    check(cell[i*side+side-1]==east[i*side],"East seam differs");
                    check(cell[(side-1)*side+i]==north[i],"North seam differs");
                    borders+=2;
                }
                for(auto height:cell)
                    check(height>=config.Minimum && height<=config.Maximum,"Height outside range");
                check(cell==GenerateSeededHeightCell(seed,x,z,config),"Generation depends on call order");
            }
    check(GenerateSeededHeightCell(42,-1,-1,config)!=GenerateSeededHeightCell(43,-1,-1,config),"Seed change had no effect");
    SeededHeightConfig golden;
    check(SampleSeededHeight(42,0,0,golden)==12912,"Golden origin changed");
    check(SampleSeededHeight(42,-16,0,golden)==13184,"Golden negative lattice changed");
    check(SampleSeededHeight(42,0,16,golden)==-5765,"Golden north lattice changed");
    config.Minimum=std::numeric_limits<std::int32_t>::min();
    config.Maximum=std::numeric_limits<std::int32_t>::max();
    (void)GenerateSeededHeightCell(42,std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),config);
    config.Intervals=0;
    bool rejected=false;
    try{(void)GenerateSeededHeightCell(0,0,0,config);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Invalid config accepted");
    std::cout<<borders<<" shared border samples, deterministic order, golden seeds and extreme coordinates passed\n";
}
