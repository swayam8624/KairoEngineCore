module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
export module Kairo.EngineCore.Scene;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Components;
import Kairo.EngineCore.RuntimeComponents;
import Kairo.Foundation.Math;
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

        /// Destroys an entity and its complete descendant subtree. Recursive
        /// ownership prevents dangling hierarchy references; an iterative walk
        /// avoids stack overflow for deeply nested imported scenes.
        void DestroyEntity(Entity entity)
        {
            Record& root = RecordFor(entity);
            if (root.Parent.has_value()) RemoveChild(*root.Parent, entity);
            std::vector<Entity> pending{ entity };
            while (!pending.empty())
            {
                const Entity current = pending.back();
                pending.pop_back();
                const auto found = m_Entities.find(current.Value);
                if (found == m_Entities.end()) continue;
                pending.insert(pending.end(), found->second.Children.begin(), found->second.Children.end());
                m_Entities.erase(found);
            }
        }
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

        /// Reparents while preserving the child's local transform. World-space
        /// preservation is an editor command because it requires an explicit
        /// transform policy. Self-parenting and ancestor cycles are rejected
        /// before either side of the relationship changes.
        void SetParent(Entity child, std::optional<Entity> parent)
        {
            Record& childRecord = RecordFor(child);
            if (parent.has_value())
            {
                (void)RecordFor(*parent);
                if (*parent == child) throw std::invalid_argument("Entity cannot parent itself.");
                for (std::optional<Entity> ancestor = parent; ancestor.has_value();
                    ancestor = RecordFor(*ancestor).Parent)
                    if (*ancestor == child)
                        throw std::invalid_argument("Entity hierarchy cannot contain a cycle.");
            }
            if (childRecord.Parent == parent) return;
            const auto oldParent = childRecord.Parent;
            if (oldParent.has_value()) RemoveChild(*oldParent, child);
            childRecord.Parent = parent;
            if (parent.has_value())
            {
                auto& children = RecordFor(*parent).Children;
                children.push_back(child);
                std::ranges::sort(children, {}, &Entity::Value);
            }
        }

        [[nodiscard]] std::optional<Entity> Parent(Entity entity) const { return RecordFor(entity).Parent; }
        [[nodiscard]] const std::vector<Entity>& Children(Entity entity) const { return RecordFor(entity).Children; }
        [[nodiscard]] std::vector<Entity> RootEntities() const
        {
            std::vector<Entity> roots;
            for (const Entity entity : Entities())
                if (!RecordFor(entity).Parent.has_value()) roots.push_back(entity);
            return roots;
        }

        /// Output: local transforms composed from the root through `entity`.
        /// Task: provide one authoritative hierarchy calculation to rendering,
        /// physics, audio, and editor adapters. The walk is iterative and the
        /// hierarchy is acyclic by construction.
        [[nodiscard]] kairo::foundation::math::Transformf WorldTransform(Entity entity) const
        {
            std::vector<Entity> ancestry;
            for (std::optional<Entity> current = entity; current.has_value();
                current = RecordFor(*current).Parent)
                ancestry.push_back(*current);
            auto world = kairo::foundation::math::Transformf::Identity();
            for (auto iterator = ancestry.rbegin(); iterator != ancestry.rend(); ++iterator)
                world *= RecordFor(*iterator).Transform.Local;
            return world;
        }

        [[nodiscard]] bool IsEnabled(Entity entity) const { return RecordFor(entity).Settings.Enabled; }
        void SetEnabled(Entity entity, bool enabled) { RecordFor(entity).Settings.Enabled = enabled; }
        [[nodiscard]] bool IsActiveInHierarchy(Entity entity) const
        {
            for (std::optional<Entity> current = entity; current.has_value();
                current = RecordFor(*current).Parent)
                if (!RecordFor(*current).Settings.Enabled) return false;
            return true;
        }
        [[nodiscard]] std::uint32_t Layer(Entity entity) const { return RecordFor(entity).Settings.Layer; }
        void SetLayer(Entity entity, std::uint32_t layer)
        {
            if (layer > MaximumSceneLayer)
                throw std::invalid_argument("Entity layer must be between 0 and 63.");
            RecordFor(entity).Settings.Layer = layer;
        }
        [[nodiscard]] const std::vector<std::string>& Tags(Entity entity) const
        {
            return RecordFor(entity).Settings.Tags;
        }
        [[nodiscard]] bool HasTag(Entity entity, std::string_view tag) const
        {
            const auto& tags = RecordFor(entity).Settings.Tags;
            return std::ranges::binary_search(tags, tag);
        }
        void AddTag(Entity entity, std::string tag)
        {
            EntitySettingsComponent::ValidateTag(tag);
            auto& tags = RecordFor(entity).Settings.Tags;
            const auto insertion = std::ranges::lower_bound(tags, tag);
            if (insertion != tags.end() && *insertion == tag) return;
            if (tags.size() >= MaximumEntityTags)
                throw std::length_error("Entity exceeds its 32-tag safety limit.");
            tags.insert(insertion, std::move(tag));
        }
        bool RemoveTag(Entity entity, std::string_view tag)
        {
            auto& tags = RecordFor(entity).Settings.Tags;
            const auto found = std::ranges::lower_bound(tags, tag);
            if (found == tags.end() || *found != tag) return false;
            tags.erase(found);
            return true;
        }

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

        /// Stores or replaces a hierarchy-preserving imported scene instance.
        /// Asset loading remains the responsibility of Editor/Player adapters.
        void SetSceneInstance(Entity entity, SceneInstanceComponent component)
        {
            component.Validate();
            RecordFor(entity).SceneInstance = std::move(component);
        }
        [[nodiscard]] bool HasSceneInstance(Entity entity) const
        {
            return RecordFor(entity).SceneInstance.has_value();
        }
        [[nodiscard]] SceneInstanceComponent& SceneInstance(Entity entity)
        {
            return RequireComponent(RecordFor(entity).SceneInstance, "scene instance");
        }
        [[nodiscard]] const SceneInstanceComponent& SceneInstance(Entity entity) const
        {
            return RequireComponent(RecordFor(entity).SceneInstance, "scene instance");
        }
        bool RemoveSceneInstance(Entity entity)
        {
            auto& component = RecordFor(entity).SceneInstance;
            const bool removed = component.has_value();
            component.reset();
            return removed;
        }

        /// Output: visible imported-scene instances in stable entity-ID order.
        [[nodiscard]] std::vector<Entity> SceneInstanceEntities() const
        {
            std::vector<Entity> result;
            for (const Entity entity : Entities())
                if (const auto& component = RecordFor(entity).SceneInstance;
                    component.has_value() && component->Visible &&
                    IsActiveInHierarchy(entity))
                    result.push_back(entity);
            return result;
        }

        /// Input: entity and projectable camera parameters.
        /// Output: stores or replaces the camera component after validation.
        /// Degeneracy: invalid projection data and a second primary camera are
        /// rejected before the scene changes, preserving the previous component.
        void SetCamera(Entity entity, CameraComponent component)
        {
            Record& record = RecordFor(entity);
            component.Validate();
            if (component.Primary)
            {
                for (const Entity candidate : CameraEntities())
                {
                    if (candidate != entity && RecordFor(candidate).Camera->Primary)
                        throw std::invalid_argument(
                            "Scene already contains a primary camera.");
                }
            }
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

        /// Output: camera entities in stable entity-ID order.
        [[nodiscard]] std::vector<Entity> CameraEntities() const
        {
            std::vector<Entity> result;
            for (const Entity entity : Entities())
                if (RecordFor(entity).Camera.has_value()) result.push_back(entity);
            return result;
        }

        /// Output: the single authored primary camera, if one exists.
        /// Degeneracy: SetCamera prevents multiple primary flags. The explicit
        /// check remains defensive for scenes produced by future low-level loaders.
        [[nodiscard]] std::optional<Entity> PrimaryCamera() const
        {
            std::optional<Entity> result;
            for (const Entity entity : CameraEntities())
            {
                if (!RecordFor(entity).Camera->Primary) continue;
                if (result.has_value())
                    throw std::logic_error("Scene contains more than one primary camera.");
                result = entity;
            }
            return result;
        }

        /// Stores a renderer-neutral authored light. Runtime shadow maps,
        /// descriptor sets, and ray-tracing emitters remain adapter-owned.
        void SetLight(Entity entity, LightComponent component)
        {
            component.Validate();
            RecordFor(entity).Light = component;
        }
        [[nodiscard]] bool HasLight(Entity entity) const { return RecordFor(entity).Light.has_value(); }
        [[nodiscard]] LightComponent& Light(Entity entity)
        {
            return RequireComponent(RecordFor(entity).Light, "light");
        }
        [[nodiscard]] const LightComponent& Light(Entity entity) const
        {
            return RequireComponent(RecordFor(entity).Light, "light");
        }
        bool RemoveLight(Entity entity)
        {
            auto& component = RecordFor(entity).Light;
            const bool removed = component.has_value();
            component.reset();
            return removed;
        }
        [[nodiscard]] std::vector<Entity> LightEntities() const
        {
            std::vector<Entity> result;
            for (const Entity entity : Entities())
                if (RecordFor(entity).Light.has_value() && IsActiveInHierarchy(entity))
                    result.push_back(entity);
            return result;
        }

        /// Stores one environment candidate. The scene may retain several
        /// candidates so future volume systems do not require a file-format
        /// break; ActiveEnvironment resolves the global candidate today.
        void SetEnvironment(Entity entity, EnvironmentComponent component)
        {
            component.Validate();
            RecordFor(entity).Environment = std::move(component);
        }
        [[nodiscard]] bool HasEnvironment(Entity entity) const
        {
            return RecordFor(entity).Environment.has_value();
        }
        [[nodiscard]] EnvironmentComponent& Environment(Entity entity)
        {
            return RequireComponent(RecordFor(entity).Environment, "environment");
        }
        [[nodiscard]] const EnvironmentComponent& Environment(Entity entity) const
        {
            return RequireComponent(RecordFor(entity).Environment, "environment");
        }
        bool RemoveEnvironment(Entity entity)
        {
            auto& component = RecordFor(entity).Environment;
            const bool removed = component.has_value();
            component.reset();
            return removed;
        }

        /// Output: enabled highest-priority environment, breaking ties by the
        /// lowest stable entity ID. Disabled entities/ancestors are excluded.
        [[nodiscard]] std::optional<Entity> ActiveEnvironment() const
        {
            std::optional<Entity> result;
            for (const Entity entity : Entities())
            {
                const auto& environment = RecordFor(entity).Environment;
                if (!environment.has_value() || !environment->Enabled ||
                    !IsActiveInHierarchy(entity)) continue;
                if (!result.has_value() || environment->Priority >
                    RecordFor(*result).Environment->Priority)
                    result = entity;
            }
            return result;
        }

        /// Attaches renderer- and VM-independent authored gameplay logic.
        /// Asset resolution occurs during scene parsing/serialization; direct
        /// construction validates identity without forcing an asset registry
        /// into the in-memory scene API.
        void SetLogic(Entity entity, LogicComponent component)
        {
            component.Validate();
            RecordFor(entity).Logic = component;
        }
        [[nodiscard]] bool HasLogic(Entity entity) const { return RecordFor(entity).Logic.has_value(); }
        [[nodiscard]] LogicComponent& Logic(Entity entity)
        {
            return RequireComponent(RecordFor(entity).Logic, "logic");
        }
        [[nodiscard]] const LogicComponent& Logic(Entity entity) const
        {
            return RequireComponent(RecordFor(entity).Logic, "logic");
        }
        bool RemoveLogic(Entity entity)
        {
            auto& component = RecordFor(entity).Logic;
            const bool removed = component.has_value();
            component.reset();
            return removed;
        }

        /// Stores renderer-independent authored physics after validating it;
        /// runtime world handles are created by a play-mode adapter.
        void SetRigidBody(Entity entity, RigidBodyComponent component)
        {
            Record& record = RecordFor(entity);
            component.Validate();
            record.RigidBody = component;
        }
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

        void SetCollider(Entity entity, ColliderComponent component)
        {
            Record& record = RecordFor(entity);
            component.Validate();
            record.Collider = component;
        }
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
                if (const auto& component = RecordFor(entity).MeshRenderer;
                    component.has_value() && component->Visible && IsActiveInHierarchy(entity))
                    result.push_back(entity);
            return result;
        }
    private:
        struct Record final
        {
            NameComponent Name;
            TransformComponent Transform;
            EntitySettingsComponent Settings;
            std::optional<Entity> Parent;
            std::vector<Entity> Children;
            std::optional<MeshRendererComponent> MeshRenderer;
            std::optional<SceneInstanceComponent> SceneInstance;
            std::optional<CameraComponent> Camera;
            std::optional<LightComponent> Light;
            std::optional<EnvironmentComponent> Environment;
            std::optional<LogicComponent> Logic;
            std::optional<RigidBodyComponent> RigidBody;
            std::optional<ColliderComponent> Collider;
        };
        std::uint32_t m_Next = 1u;
        std::unordered_map<std::uint32_t, Record> m_Entities;
        void RemoveChild(Entity parent, Entity child)
        {
            auto& children = RecordFor(parent).Children;
            const auto found = std::ranges::lower_bound(children, child.Value, {}, &Entity::Value);
            if (found == children.end() || *found != child)
                throw std::logic_error("Scene hierarchy parent/child records disagree.");
            children.erase(found);
        }
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
