module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
export module Kairo.EngineCore.Scene;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Components;
import Kairo.EngineCore.RuntimeComponents;
export namespace kairo::engine
{
    /// Input: named entity requests and component updates. Output: a stable
    /// scene registry. Task: own IDs/components without selecting a renderer,
    /// physics backend, or ECS storage library prematurely.
    class Scene final
    {
    public:
        [[nodiscard]] Entity CreateEntity(std::string name = "Entity")
        {
            if (m_Next == 0u)
                throw std::overflow_error("Scene exhausted its 32-bit entity ID space.");
            const Entity id{ m_Next };
            return CreateEntityWithID(id, std::move(name));
        }

        /// Input: a non-zero scene-local ID and entity name.
        /// Output: a new entity retaining the requested stable identity.
        /// Task: restore serialized scenes without making file order affect IDs.
        /// Degeneracy: zero and duplicate IDs are rejected; restoring an ID at
        /// or above the allocation cursor advances that cursor without wrapping
        /// into the reserved invalid ID.
        [[nodiscard]] Entity CreateEntityWithID(Entity id, std::string name = "Entity")
        {
            if (!id) throw std::invalid_argument("A scene entity ID cannot be zero.");
            if (m_Entities.contains(id.Value)) throw std::invalid_argument("Scene already contains this entity ID.");
            Record record;
            record.Name.Value = std::move(name);
            m_Entities.emplace(id.Value, std::move(record));
            if (m_Next != 0u && id.Value >= m_Next)
                m_Next = id.Value == std::numeric_limits<std::uint32_t>::max() ? 0u : id.Value + 1u;
            return id;
        }
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

        /// Input: entity and a validated renderer-independent asset binding.
        /// Output: stores or replaces that entity's mesh renderer component.
        /// Task: make visible scene objects discoverable by renderer adapters
        /// while keeping asset loading and GPU ownership outside EngineCore.
        void SetMeshRenderer(Entity entity, MeshRendererComponent component)
        {
            Record& record = RecordFor(entity);
            component.Validate();
            record.MeshRenderer = std::move(component);
        }
        [[nodiscard]] bool HasMeshRenderer(Entity entity) const { return RecordFor(entity).MeshRenderer.has_value(); }
        [[nodiscard]] MeshRendererComponent& MeshRenderer(Entity entity) { return RequireComponent(RecordFor(entity).MeshRenderer, "mesh renderer"); }
        [[nodiscard]] const MeshRendererComponent& MeshRenderer(Entity entity) const { return RequireComponent(RecordFor(entity).MeshRenderer, "mesh renderer"); }
        bool RemoveMeshRenderer(Entity entity)
        {
            auto& component = RecordFor(entity).MeshRenderer;
            const bool removed = component.has_value();
            component.reset();
            return removed;
        }

        /// Input: entity and projectable camera parameters.
        /// Output: stores or replaces the camera component after validation.
        /// Degeneracy: invalid FOV or clipping planes are rejected before the
        /// scene changes, preserving the previous component when replacement fails.
        void SetCamera(Entity entity, CameraComponent component)
        {
            Record& record = RecordFor(entity);
            component.Validate();
            record.Camera = component;
        }
        [[nodiscard]] bool HasCamera(Entity entity) const { return RecordFor(entity).Camera.has_value(); }
        [[nodiscard]] CameraComponent& Camera(Entity entity) { return RequireComponent(RecordFor(entity).Camera, "camera"); }
        [[nodiscard]] const CameraComponent& Camera(Entity entity) const { return RequireComponent(RecordFor(entity).Camera, "camera"); }
        bool RemoveCamera(Entity entity)
        {
            auto& component = RecordFor(entity).Camera;
            const bool removed = component.has_value();
            component.reset();
            return removed;
        }

        /// Physics IDs are opaque runtime handles. Presence of the component,
        /// not a sentinel numeric value, determines whether an entity is bound.
        void SetRigidBody(Entity entity, RigidBodyComponent component) { RecordFor(entity).RigidBody = component; }
        [[nodiscard]] bool HasRigidBody(Entity entity) const { return RecordFor(entity).RigidBody.has_value(); }
        [[nodiscard]] RigidBodyComponent& RigidBody(Entity entity) { return RequireComponent(RecordFor(entity).RigidBody, "rigid body"); }
        [[nodiscard]] const RigidBodyComponent& RigidBody(Entity entity) const { return RequireComponent(RecordFor(entity).RigidBody, "rigid body"); }
        bool RemoveRigidBody(Entity entity)
        {
            auto& component = RecordFor(entity).RigidBody;
            const bool removed = component.has_value();
            component.reset();
            return removed;
        }

        void SetCollider(Entity entity, ColliderComponent component) { RecordFor(entity).Collider = component; }
        [[nodiscard]] bool HasCollider(Entity entity) const { return RecordFor(entity).Collider.has_value(); }
        [[nodiscard]] ColliderComponent& Collider(Entity entity) { return RequireComponent(RecordFor(entity).Collider, "collider"); }
        [[nodiscard]] const ColliderComponent& Collider(Entity entity) const { return RequireComponent(RecordFor(entity).Collider, "collider"); }
        bool RemoveCollider(Entity entity)
        {
            auto& component = RecordFor(entity).Collider;
            const bool removed = component.has_value();
            component.reset();
            return removed;
        }

        /// Output: visible mesh entities in deterministic entity-ID order.
        /// Task: give render extraction a stable traversal without exposing
        /// optional component storage or unordered-map iteration order.
        [[nodiscard]] std::vector<Entity> RenderableEntities() const
        {
            std::vector<Entity> result;
            for (const Entity entity : Entities())
                if (const auto& component = RecordFor(entity).MeshRenderer; component.has_value() && component->Visible)
                    result.push_back(entity);
            return result;
        }
    private:
        struct Record final
        {
            NameComponent Name;
            TransformComponent Transform;
            std::optional<MeshRendererComponent> MeshRenderer;
            std::optional<CameraComponent> Camera;
            std::optional<RigidBodyComponent> RigidBody;
            std::optional<ColliderComponent> Collider;
        };
        std::uint32_t m_Next = 1u;
        std::unordered_map<std::uint32_t, Record> m_Entities;
        [[nodiscard]] Record& RecordFor(Entity entity) { auto it = m_Entities.find(entity.Value); if (it == m_Entities.end()) throw std::out_of_range("Scene does not contain this entity."); return it->second; }
        [[nodiscard]] const Record& RecordFor(Entity entity) const
        {
            const auto it = m_Entities.find(entity.Value);
            if (it == m_Entities.end()) throw std::out_of_range("Scene does not contain this entity.");
            return it->second;
        }

        template<class Component>
        [[nodiscard]] static Component& RequireComponent(std::optional<Component>& component, const char* name)
        {
            if (!component.has_value()) throw std::logic_error(std::string("Entity has no ") + name + " component.");
            return *component;
        }

        template<class Component>
        [[nodiscard]] static const Component& RequireComponent(const std::optional<Component>& component, const char* name)
        {
            if (!component.has_value()) throw std::logic_error(std::string("Entity has no ") + name + " component.");
            return *component;
        }
    };
}
