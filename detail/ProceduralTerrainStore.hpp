#pragma once

#include <cstddef>
#include <exception>
#include <map>
#include <stdexcept>
#include <utility>

namespace kairo::engine::detail
{
    // CPU-only ownership. A completed load publishes a mesh once; failed
    // construction leaves no resident entry. The streaming planner remains
    // responsible for budgets, distance policy and request ordering.
    template<class Coordinate,class Mesh>
    class ProceduralTerrainStore final
    {
    public:
        template<class Generate>
        void Load(Coordinate coordinate,Generate&& generate)
        {
            if(m_Resident.contains(coordinate))
                throw std::logic_error("Procedural terrain cell is already resident.");
            auto mesh=generate(coordinate);
            m_Resident.emplace(coordinate,std::move(mesh));
        }

        void Unload(Coordinate coordinate)
        {
            if(m_Resident.erase(coordinate)!=1u)
                throw std::logic_error("Procedural terrain cell has no owned mesh.");
        }

        [[nodiscard]] const Mesh* Find(Coordinate coordinate) const noexcept
        {
            const auto found=m_Resident.find(coordinate);
            return found==m_Resident.end()?nullptr:&found->second;
        }

        [[nodiscard]] const std::map<Coordinate,Mesh>& Cells() const noexcept
        { return m_Resident; }

    private:
        std::map<Coordinate,Mesh> m_Resident;
    };

    struct ProceduralCompletionStats
    {
        std::size_t Loaded=0,Unloaded=0,FailedLoads=0,FailedUnloads=0;
        std::exception_ptr FirstFailure;
    };

    // The plan has already reserved all requests. Complete every request
    // even after a generation failure. No allocation is required to record
    // failures; one exception_ptr preserves diagnostic detail.
    template<class Store,class Runtime,class Plan,class Generate>
    [[nodiscard]] ProceduralCompletionStats ApplyProceduralTerrainPlan(
        Store& store,Runtime& runtime,const Plan& plan,Generate&& generate)
    {
        ProceduralCompletionStats stats;
        for(const auto& request:plan.Unloads)
        {
            bool success=false;
            try{store.Unload(request.Coordinate);success=true;}
            catch(...){++stats.FailedUnloads;
                if(!stats.FirstFailure) stats.FirstFailure=std::current_exception();}
            runtime.CompleteUnload(request.Coordinate,success);
            if(success)++stats.Unloaded;
        }
        for(const auto& request:plan.Loads)
        {
            bool success=false;
            try{store.Load(request.Coordinate,generate);success=true;}
            catch(...){++stats.FailedLoads;
                if(!stats.FirstFailure) stats.FirstFailure=std::current_exception();}
            runtime.CompleteLoad(request.Coordinate,success);
            if(success)++stats.Loaded;
        }
        return stats;
    }
}
