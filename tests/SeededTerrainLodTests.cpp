#include "../detail/SeededTerrainLod.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
struct Vertex {std::array<float,3> Position{},Normal{};std::array<float,2> TexCoord{};friend bool operator==(const Vertex&,const Vertex&)=default;};
struct Mesh {std::vector<Vertex> Vertices;std::vector<std::uint32_t> Indices;bool HasNormals=false,HasTexCoords=false;};
void Check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
using Key=std::pair<int,int>;
using Edge=std::pair<Key,Key>;
Key Grid(const Vertex& v){return {int(v.Position[0]),int(v.Position[2])};}
}
int main()
{
    using namespace kairo::engine::detail;
    SeededHeightConfig cfg;
    cfg.Intervals=32;cfg.LatticeSpacing=7;
    unsigned long long triangles=0,boundaries=0;
    for(std::uint64_t seed:{0ull,42ull,987654ull})
        for(int cz=-2;cz<=2;++cz)
            for(int cx=-2;cx<=2;++cx)
            {
                auto fine=GenerateSeededTerrainMesh<Mesh>(seed,cx,cz,cfg,1000);
                const auto east=GenerateSeededTerrainMesh<Mesh>(seed,cx+1,cz,cfg,1000);
                std::map<Key,Vertex> reference;
                for(const auto& v:fine.Vertices)reference.emplace(Grid(v),v);
                for(unsigned stride:{1u,2u,4u,8u,16u,32u})
                {
                    auto mesh=GenerateSeededTerrainLod<Mesh>(seed,cx,cz,cfg,1000,stride);
                    if(stride==1)Check(mesh.Vertices==fine.Vertices&&mesh.Indices==fine.Indices,"Reference changed");
                    std::set<Key> unique;
                    for(const auto& v:mesh.Vertices)
                    {
                        const auto key=Grid(v);
                        Check(unique.insert(key).second,"Duplicate vertex");
                        Check(reference.at(key)==v,"LOD height, normal or UV differs");
                        if(key.first==32)
                        {
                            const auto& other=east.Vertices.at(static_cast<std::size_t>(key.second)*33);
                            Check(v.Position[1]==other.Position[1]&&v.Normal==other.Normal,"Adjacent seam");
                        }
                    }
                    std::map<Edge,unsigned> edges;
                    double area=0;
                    for(std::size_t i=0;i<mesh.Indices.size();i+=3)
                    {
                        const auto a=Grid(mesh.Vertices.at(mesh.Indices[i]));
                        const auto b=Grid(mesh.Vertices.at(mesh.Indices[i+1]));
                        const auto c=Grid(mesh.Vertices.at(mesh.Indices[i+2]));
                        const double twice=(b.second-a.second)*(c.first-a.first)-(b.first-a.first)*(c.second-a.second);
                        Check(twice>0,"Degenerate or inverted triangle");
                        area+=twice/2;
                        for(auto edge:{Edge{a,b},Edge{b,c},Edge{c,a}})
                        {if(edge.second<edge.first)std::swap(edge.first,edge.second);++edges[edge];}
                        ++triangles;
                    }
                    Check(area==1024,"Incomplete projected cell coverage");
                    unsigned perimeter=0;
                    for(const auto& [edge,count]:edges)
                    {
                        const auto [a,b]=edge;
                        const bool outer=(a.first==b.first&&(a.first==0||a.first==32))||
                            (a.second==b.second&&(a.second==0||a.second==32));
                        Check(count==(outer?1u:2u),"Nonmanifold edge or interior T-junction");
                        if(outer)
                        {
                            Check(std::abs(a.first-b.first)+std::abs(a.second-b.second)==1,"Coarsened perimeter");
                            ++perimeter;++boundaries;
                        }
                    }
                    Check(perimeter==128,"Missing perimeter segment");
                    if(stride>1)Check(mesh.Indices.size()<fine.Indices.size(),"No reduction");
                }
            }
    for(unsigned stride:{1u,2u,4u,8u,16u,32u})
    {
        const auto mesh=GenerateSeededTerrainLod<Mesh>(42,0,0,cfg,1000,stride);
        std::cout<<"stride "<<stride<<": "<<mesh.Vertices.size()<<" vertices, "<<mesh.Indices.size()/3<<" triangles\n";
    }
    bool rejected=false;
    try{(void)GenerateSeededTerrainLod<Mesh>(42,0,0,cfg,1000,3);}
    catch(const std::invalid_argument&){rejected=true;}
    Check(rejected,"Invalid LOD stride accepted");
    std::cout<<triangles<<" triangles, "<<boundaries<<" invariant perimeter edges and manifold coverage passed\n";
}
