#include "../detail/ProceduralTerrainStore.hpp"
#include <iostream>
#include <vector>
#include <string>
namespace {
struct Request{int Coordinate;};
struct Plan{std::vector<Request> Loads,Unloads;};
struct Runtime{
    std::map<int,bool> Completed;
    void CompleteLoad(int c,bool ok){if(!Completed.emplace(c,ok).second)throw std::logic_error("Duplicate completion");}
    void CompleteUnload(int c,bool ok){CompleteLoad(c,ok);}
};
void Check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
}
int main()
{
    using namespace kairo::engine::detail;
    ProceduralTerrainStore<int,std::vector<int>> store;
    Runtime runtime;
    const auto generate=[](int c){
        if(c%7==0)throw std::runtime_error("Injected generation failure");
        return std::vector<int>(16,c);
    };
    Plan plan;
    for(int i=1;i<=10000;++i)plan.Loads.push_back({i});
    auto stats=ApplyProceduralTerrainPlan(store,runtime,plan,generate);
    Check(stats.Loaded==8572&&stats.FailedLoads==1428&&stats.FirstFailure,"Load counts differ");
    Check(runtime.Completed.size()==10000&&store.Cells().size()==8572,"Incomplete reservations or ownership");
    for(int i=1;i<=10000;++i){
        Check(runtime.Completed.at(i)==(i%7!=0),"Incorrect completion");
        const auto* mesh=store.Find(i);
        Check((mesh!=nullptr)==(i%7!=0),"Failed load published");
        if(mesh)Check(mesh->size()==16&&mesh->front()==i,"Wrong mesh ownership");
    }
    runtime.Completed.clear();
    plan.Loads.clear();
    for(int i=1;i<=10000;++i)plan.Unloads.push_back({i});
    stats=ApplyProceduralTerrainPlan(store,runtime,plan,generate);
    Check(stats.Unloaded==8572&&stats.FailedUnloads==1428,"Unload counts differ");
    Check(store.Cells().empty()&&runtime.Completed.size()==10000,"Unload leaked ownership or reservations");
    runtime.Completed.clear();
    plan.Unloads.clear();
    plan.Loads={{1}};
    stats=ApplyProceduralTerrainPlan(store,runtime,plan,generate);
    Check(stats.Loaded==1&&store.Find(1)->front()==1,"Revisit failed");
    std::cout<<"20001 request completions, 2856 injected failures and revisit ownership passed\n";
}
