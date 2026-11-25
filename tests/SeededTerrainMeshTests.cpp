#include "../detail/SeededTerrainMesh.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
struct Vertex { std::array<float,3> Position{},Normal{}; std::array<float,2> TexCoord{}; };
struct Mesh { std::vector<Vertex> Vertices; std::vector<std::uint32_t> Indices;
    bool HasNormals=false,HasTexCoords=false; };
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}

int main()
{
    using namespace kairo::engine::detail;
    SeededHeightConfig config;
    config.Intervals=31;config.LatticeSpacing=7;
    constexpr std::uint32_t spacing=750;
    const std::size_t side=config.Intervals+1;
    std::size_t seams=0,triangles=0;
    for(std::uint64_t seed:{0ull,42ull,999999ull})
        for(std::int32_t z=-4;z<4;++z)
            for(std::int32_t x=-4;x<4;++x)
            {
                const auto a=GenerateSeededTerrainMesh<Mesh>(seed,x,z,config,spacing);
                const auto e=GenerateSeededTerrainMesh<Mesh>(seed,x+1,z,config,spacing);
                const auto n=GenerateSeededTerrainMesh<Mesh>(seed,x,z+1,config,spacing);
                const auto heights=GenerateSeededHeightCell(seed,x,z,config);
                Check(a.HasNormals&&a.HasTexCoords&&a.Vertices.size()==side*side &&
                    a.Indices.size()==6u*config.Intervals*config.Intervals,"Mesh shape incorrect");
                for(std::size_t i=0;i<a.Vertices.size();++i)
                {
                    const auto& v=a.Vertices[i];
                    Check(v.Position[1]==static_cast<float>(static_cast<double>(heights[i])/1000.0),"Mesh height differs from generator");
                    double norm=0;
                    for(float k:v.Normal)norm+=static_cast<double>(k)*k;
                    Check(std::abs(norm-1)<1e-6 && v.Normal[1]>0,"Invalid normal");
                }
                for(std::size_t i=0;i<side;++i)
                {
                    const auto& east=a.Vertices[i*side+side-1];
                    const auto& west=e.Vertices[i*side];
                    const auto& north=a.Vertices[(side-1)*side+i];
                    const auto& south=n.Vertices[i];
                    Check(east.Position[1]==west.Position[1] && east.Normal==west.Normal &&
                        east.Position[2]==west.Position[2],"East edge normal or height seam");
                    Check(north.Position[1]==south.Position[1] && north.Normal==south.Normal &&
                        north.Position[0]==south.Position[0],"North edge normal or height seam");
                    Check(east.TexCoord[0]==1 && west.TexCoord[0]==0 &&
                        north.TexCoord[1]==1 && south.TexCoord[1]==0,"Tile UV edge mismatch");
                    seams+=2;
                }
                for(std::size_t i=0;i<a.Indices.size();i+=3)
                {
                    const auto& v=a.Vertices.at(a.Indices[i]).Position;
                    const auto& w=a.Vertices.at(a.Indices[i+1]).Position;
                    const auto& u=a.Vertices.at(a.Indices[i+2]).Position;
                    const double y=(w[2]-v[2])*(u[0]-v[0])-(w[0]-v[0])*(u[2]-v[2]);
                    Check(y>0,"Triangle winding or zero horizontal area");
                    ++triangles;
                }
            }
    config.Minimum=config.Maximum=1234;
    const auto flat=GenerateSeededTerrainMesh<Mesh>(42,-1,0,config,spacing);
    for(const auto& v:flat.Vertices)
        Check(v.Normal==std::array<float,3>{0,1,0},"Flat terrain normal incorrect");
    bool rejected=false;
    try{(void)GenerateSeededTerrainMesh<Mesh>(42,0,0,config,0);}
    catch(const std::invalid_argument&){rejected=true;}
    Check(rejected,"Zero spacing accepted");
    std::cout<<seams<<" shared height/normal samples, "<<triangles<<" upward triangles and exact height mapping passed\n";
}
