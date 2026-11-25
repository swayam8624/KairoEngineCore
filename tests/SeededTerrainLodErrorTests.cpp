#include "../detail/SeededTerrainLodError.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <random>

namespace {
using namespace kairo::engine::detail;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
int main()
{
    // Every integer point inside this coarse triangle has zero height.
    // Its sloping edge intersects the fine diagonal at (4/3,2/3).
    // The fine vertex (2,0) outside the triangle contributes 100/3 mm.
    std::vector<std::int32_t> synthetic(9,0);
    synthetic[2]=100;
    std::uint64_t error=0;
    MeasureLodTriangle({0,0,0},{2,1,0},{0,2,0},synthetic,2,error);
    Check(error==34,"Fractional crossing maximum was missed");
    synthetic[2]=-100;error=0;
    MeasureLodTriangle({0,0,0},{2,1,0},{0,2,0},synthetic,2,error);
    Check(error==34,"Signed fractional maximum was missed");

    SeededHeightConfig cfg;cfg.Intervals=16;cfg.LatticeSpacing=3;
    std::mt19937 rng(73519);
    std::uniform_int_distribution<int> coordinate(-100,100);
    std::uint64_t comparisons=0;
    for(unsigned scene=0;scene<24;++scene)
    {
        const int cx=coordinate(rng),cz=coordinate(rng);
        const auto seed=static_cast<std::uint64_t>(rng());
        if(scene==0){cfg.Minimum=std::numeric_limits<std::int32_t>::min();
            cfg.Maximum=std::numeric_limits<std::int32_t>::max();}
        else{cfg.Minimum=-4000;cfg.Maximum=12000;}
        const auto heights=GenerateSeededHeightCell(seed,cx,cz,cfg);
        for(unsigned stride:{1u,2u,4u,8u,16u})
        {
            const auto bound=MeasureSeededTerrainLodError(seed,cx,cz,cfg,stride);
            if(stride==1)Check(bound==0,"Full resolution must be exact");
            Check(bound<=static_cast<std::uint64_t>(static_cast<std::int64_t>(cfg.Maximum)-cfg.Minimum),"Error exceeds height range");
            const auto mesh=GenerateSeededTerrainLod<LodErrorMesh>(seed,cx,cz,cfg,1000,stride);
            const auto pt=[&](unsigned id)
            {
                const auto& v=mesh.Vertices[id];
                const auto x=static_cast<std::int64_t>(v.Position[0]),z=static_cast<std::int64_t>(v.Position[2]);
                return LodErrorPoint{x,z,heights[static_cast<std::size_t>(z*17+x)]};
            };
            // Independent dense barycentric sampling, including fractions
            // inside fine triangles. This is a lower-bound oracle, not the proof.
            for(std::size_t i=0;i<mesh.Indices.size();i+=3)
            {
                const auto a=pt(mesh.Indices[i]),b=pt(mesh.Indices[i+1]),c=pt(mesh.Indices[i+2]);
                constexpr std::int64_t den=12;
                for(std::int64_t u=0;u<=den;++u)
                    for(std::int64_t v=0;v<=den-u;++v)
                    {
                        const auto w=den-u-v;
                        const auto xn=w*a.X+u*b.X+v*c.X,zn=w*a.Z+u*b.Z+v*c.Z;
                        const double x=static_cast<double>(xn)/den,z=static_cast<double>(zn)/den;
                        const auto ix=std::min(static_cast<int>(x),15),iz=std::min(static_cast<int>(z),15);
                        const double fx=x-ix,fz=z-iz;
                        const auto h=[&](int px,int pz){return double(heights.at(static_cast<std::size_t>(pz*17+px)));};
                        const double fine=fx+fz<=1?
                            (1-fx-fz)*h(ix,iz)+fx*h(ix+1,iz)+fz*h(ix,iz+1):
                            (fx+fz-1)*h(ix+1,iz+1)+(1-fx)*h(ix,iz+1)+(1-fz)*h(ix+1,iz);
                        const double coarse=static_cast<double>(w*a.H+u*b.H+v*c.H)/den;
                        Check(std::abs(coarse-fine)<=static_cast<double>(bound)+1e-5,"Sample exceeded exact bound");
                        ++comparisons;
                    }
            }
        }
    }
    cfg.Minimum=cfg.Maximum=123;
    Check(MeasureSeededTerrainLodError(42,-5,7,cfg,16)==0,"Flat cell error not zero");
    cfg.Intervals=1024;cfg.Minimum=-4000;cfg.Maximum=12000;
    const auto large=MeasureSeededTerrainLodError(42,0,0,cfg,1024);
    Check(large<=16000,"Largest supported cell error exceeds range");
    std::cout<<comparisons<<" dense triangle samples, signed fractional crossings, extreme heights and 1024-cell bound passed\n";
    cfg.Intervals=32;cfg.LatticeSpacing=7;
    for(unsigned stride:{1u,2u,4u,8u,16u,32u})
        std::cout<<"stride "<<stride<<": error "<<MeasureSeededTerrainLodError(42,0,0,cfg,stride)<<" mm\n";
}
