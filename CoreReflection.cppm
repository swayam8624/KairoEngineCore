module;

#include <string_view>
#include <optional>
#include <new>
#include <stdexcept>
#include <vector>

export module Kairo.EngineCore.Reflection;

import Kairo.EngineCore.Components;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.RuntimeComponents;
import Kairo.EngineCore.Scene;
import Kairo.Reflection;

export namespace kairo::engine
{
    /// A transient link between a Scene-owned component and its stable
    /// reflection type key. Input: a live Scene/entity pair. Output: a
    /// non-owning component address. Task: let tooling enumerate engine-owned
    /// components without taking a dependency on Scene's private storage.
    /// Lifetime: the pointer is valid only until a structural scene mutation
    /// (such as creating or destroying an entity) or the next Scene lifetime
    /// transition. Consumers must resolve it again before retaining it.
    struct ReflectedSceneComponent final
    {
        std::string_view TypeKey;
        void* Object = nullptr;
    };

    /// Input: a live scene entity. Output: its reflectable components in a
    /// deterministic component-type order. Task: give inspectors, property
    /// search, and serializers one canonical traversal that cannot accidentally
    /// infer component presence from opaque IDs or UI-specific state.
    [[nodiscard]] inline std::vector<ReflectedSceneComponent> EnumerateReflectedComponents(
        Scene& scene, Entity entity)
    {
        std::vector<ReflectedSceneComponent> result;
        result.reserve(5u);
        result.push_back({ "Kairo.Engine.NameComponent", &scene.Name(entity) });
        if (scene.HasCamera(entity))
            result.push_back({ "Kairo.Engine.CameraComponent", &scene.Camera(entity) });
        if (scene.HasMeshRenderer(entity))
            result.push_back({ "Kairo.Engine.MeshRendererComponent", &scene.MeshRenderer(entity) });
        if (scene.HasRigidBody(entity))
            result.push_back({ "Kairo.Engine.RigidBodyComponent", &scene.RigidBody(entity) });
        if (scene.HasCollider(entity))
            result.push_back({ "Kairo.Engine.ColliderComponent", &scene.Collider(entity) });
        return result;
    }

    /// Input: a live scene entity and a registered EngineCore reflection key.
    /// Output: the matching component address. Task: resolve an object again
    /// at command execution time so undo/redo does not retain pointers across
    /// scene mutations. Throws when a component is not present on the entity.
    [[nodiscard]] inline void* ResolveReflectedComponent(Scene& scene, Entity entity,
        std::string_view typeKey)
    {
        for (const ReflectedSceneComponent component : EnumerateReflectedComponents(scene, entity))
            if (component.TypeKey == typeKey) return component.Object;
        throw std::logic_error("Entity does not expose reflected component: " + std::string(typeKey));
    }

    /// Input: a component object previously resolved by TypeKey. Output: no
    /// value on success. Task: preserve semantic component invariants after a
    /// scalar reflection write. Most simple bindings need no extra validation;
    /// camera edits must retain a projectable FOV/near/far relationship.
    inline void ValidateReflectedComponent(std::string_view typeKey, const void* object)
    {
        if (object == nullptr) throw std::invalid_argument("Reflected component validation requires a non-null object.");
        if (typeKey == "Kairo.Engine.CameraComponent")
            static_cast<const CameraComponent*>(object)->Validate();
    }

    /// Input: an empty or independently managed reflection registry.
    /// Output: registers EngineCore's scalar inspector descriptors.
    /// Task: provide one engine-owned metadata catalog for editors, serializers,
    /// search, and future graph/property adapters. The function does not own
    /// scenes or register runtime objects; it only describes their fields.
    /// Precondition: callers must not register the same type keys elsewhere.
    inline void RegisterEngineCoreReflection(kairo::reflection::ReflectionRegistry& registry)
    {
        using namespace kairo::reflection;

        registry.Register({
            .Key = "Kairo.Engine.NameComponent",
            .DisplayName = "Name",
            .Category = "Core",
            .Properties = {
                MakeMemberProperty<NameComponent>({ "value", "Name", "General", "Entity display name", PropertyFlags::None,
                    std::nullopt, 4096u }, &NameComponent::Value)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.CameraComponent",
            .DisplayName = "Camera",
            .Category = "Rendering",
            .Properties = {
                MakeMemberProperty<CameraComponent>({ "vertical-fov-radians", "Vertical FOV", "Lens", "Vertical perspective field of view in radians",
                    PropertyFlags::None, NumericRange{ 0.001, 3.14059265, 0.001 }, 0u }, &CameraComponent::VerticalFovRadians),
                MakeMemberProperty<CameraComponent>({ "near-plane", "Near Plane", "Clipping", "Near clipping distance", PropertyFlags::None,
                    NumericRange{ 0.000001, 1'000'000.0, 0.01 }, 0u }, &CameraComponent::NearPlane),
                MakeMemberProperty<CameraComponent>({ "far-plane", "Far Plane", "Clipping", "Far clipping distance", PropertyFlags::None,
                    NumericRange{ 0.000001, 10'000'000.0, 1.0 }, 0u }, &CameraComponent::FarPlane),
                MakeMemberProperty<CameraComponent>({ "primary", "Primary", "General", "Selects this camera as the primary scene camera",
                    PropertyFlags::None, std::nullopt, 0u }, &CameraComponent::Primary)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.MeshRendererComponent",
            .DisplayName = "Mesh Renderer",
            .Category = "Rendering",
            .Properties = {
                MakeMemberProperty<MeshRendererComponent>({ "visible", "Visible", "General", "Controls render extraction visibility",
                    PropertyFlags::None, std::nullopt, 0u }, &MeshRendererComponent::Visible)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.RigidBodyComponent",
            .DisplayName = "Rigid Body Binding",
            .Category = "Physics",
            .Properties = {
                MakeMemberProperty<RigidBodyComponent>({ "body", "Body ID", "Binding", "Opaque PhysicsWorld body identifier",
                    PropertyFlags::Advanced, std::nullopt, 0u }, &RigidBodyComponent::Body)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.ColliderComponent",
            .DisplayName = "Collider Binding",
            .Category = "Physics",
            .Properties = {
                MakeMemberProperty<ColliderComponent>({ "collider", "Collider ID", "Binding", "Opaque PhysicsWorld collider identifier",
                    PropertyFlags::Advanced, std::nullopt, 0u }, &ColliderComponent::Collider)
            }
        });
    }
}
