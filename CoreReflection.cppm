module;

#include <string_view>
#include <optional>
#include <new>
#include <vector>

export module Kairo.EngineCore.Reflection;

import Kairo.EngineCore.Components;
import Kairo.EngineCore.RuntimeComponents;
import Kairo.Reflection;

export namespace kairo::engine
{
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
