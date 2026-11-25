#include "../detail/SeededTerrainSurface.hpp"
#include "../detail/SeededTerrainMesh.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
namespace {
struct Vertex {std::array<float,3> Position{},Normal{};std::array<float,2> TexCoord{};};
struct Mesh {std::vector<Vertex> Vertices;std::vector<std::uint32_t> Indices;
    bool HasNormals=false,HasTexCoords=false;};
void Check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
}
int main()
{
    using namespace kairo::engine::detail;
    SeededHeightConfig config;config.Intervals=31;config.LatticeSpacing=7;
    constexpr unsigned spacing=750;
    constexpr double step=.75;
    const auto mesh=GenerateSeededTerrainMesh<Mesh>(42,-3,7,config,spacing);
    std::mt19937 rng(82901);
    std::uniform_real_distribution<double> unit(0,1);
    std::size_t tested=0;
    for(unsigned z=0;z<config.Intervals;++z)
        for(unsigned x=0;x<config.Intervals;++x)
            for(unsigned j=0;j<32;++j)
            {
                const double u=unit(rng),v=unit(rng);
                const auto sample=QuerySeededTerrainSurface(42,-3,7,config,spacing,(x+u)*step,(z+v)*step);
                Check(sample.has_value(),"Interior query rejected");
                const auto tri=(z*config.Intervals+x)*6u+(u+v<=1?0u:3u);
                const auto& a=mesh.Vertices[mesh.Indices[tri]].Position;
                const auto& b=mesh.Vertices[mesh.Indices[tri+1]].Position;
                const auto& c=mesh.Vertices[mesh.Indices[tri+2]].Position;
                const std::array<double,3> ab{double(b[0])-a[0],double(b[1])-a[1],double(b[2])-a[2]};
                const std::array<double,3> ac{double(c[0])-a[0],double(c[1])-a[1],double(c[2])-a[2]};
                const std::array<double,3> n{ab[1]*ac[2]-ab[2]*ac[1],
                    ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};
                const double y=a[1]-(n[0]*((x+u)*step-a[0])+n[2]*((z+v)*step-a[2]))/n[1];
                Check(std::abs(y-sample->HeightMeters)<5e-6,"Query disagrees with generated triangle");
                const double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                for(unsigned axis=0;axis<3;++axis)
                    Check(std::abs(n[axis]/length-sample->Normal[axis])<5e-6,"Face normal differs");
                ++tested;
            }
    const double extent=config.Intervals*step;
    for(unsigned i=0;i<=1000;++i)
    {
        const double t=extent*i/1000;
        const auto a=QuerySeededTerrainSurface(42,-1,0,config,spacing,extent,t);
        const auto b=QuerySeededTerrainSurface(42,0,0,config,spacing,0,t);
        Check(a && b && std::abs(a->HeightMeters-b->HeightMeters)<1e-10,"Neighbor query seam");
    }
    Check(!QuerySeededTerrainSurface(42,0,0,config,spacing,-.01,0),"Outside query accepted");
    Check(!QuerySeededTerrainSurface(42,0,0,config,spacing,extent+.01,0),"Far query accepted");
    std::cout<<tested<<" triangle surface queries, 1001 boundary comparisons and outside rejection passed\n";
}
