module;
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
export module Kairo.EngineCore.Scene;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Components;
export namespace kairo::engine
{
    /// Input: named entity requests and component updates. Output: a stable
    /// scene registry. Task: own IDs/components without selecting a renderer,
    /// physics backend, or ECS storage library prematurely.
    class Scene final
    {
    public:
        [[nodiscard]] Entity CreateEntity(std::string name = "Entity") { const Entity id{ m_Next++ }; m_Entities.emplace(id.Value, Record{ std::move(name), {} }); return id; }
        void DestroyEntity(Entity entity) { if (m_Entities.erase(entity.Value) == 0u) throw std::out_of_range("Scene does not contain this entity."); }
        [[nodiscard]] bool Contains(Entity entity) const noexcept { return m_Entities.contains(entity.Value); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_Entities.size(); }
        /// Output: entity IDs in deterministic ascending creation order.
        /// Task: support hierarchy, serialization, and system iteration without
        /// exposing the scene's component storage implementation.
        [[nodiscard]] std::vector<Entity> Entities() const
        {
            std::vector<Entity> entities;
            entities.reserve(m_Entities.size());
            for (const auto& [value, record] : m_Entities) entities.push_back({ value });
            std::sort(entities.begin(), entities.end(), [](Entity a, Entity b) { return a.Value < b.Value; });
            return entities;
        }
        [[nodiscard]] TransformComponent& Transform(Entity entity) { return RecordFor(entity).Transform; }
        [[nodiscard]] const TransformComponent& Transform(Entity entity) const { return RecordFor(entity).Transform; }
        [[nodiscard]] NameComponent& Name(Entity entity) { return RecordFor(entity).Name; }
        [[nodiscard]] const NameComponent& Name(Entity entity) const { return RecordFor(entity).Name; }
    private:
        struct Record final { NameComponent Name; TransformComponent Transform; };
        std::uint32_t m_Next = 1u;
        std::unordered_map<std::uint32_t, Record> m_Entities;
        [[nodiscard]] Record& RecordFor(Entity entity) { auto it = m_Entities.find(entity.Value); if (it == m_Entities.end()) throw std::out_of_range("Scene does not contain this entity."); return it->second; }
        [[nodiscard]] const Record& RecordFor(Entity entity) const { return const_cast<Scene*>(this)->RecordFor(entity); }
    };
}
