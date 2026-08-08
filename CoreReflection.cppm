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
        result.reserve(8u);
        result.push_back({ "Kairo.Engine.NameComponent", &scene.Name(entity) });
        if (scene.HasCamera(entity))
            result.push_back({ "Kairo.Engine.CameraComponent", &scene.Camera(entity) });
        if (scene.HasEnvironment(entity))
            result.push_back({ "Kairo.Engine.EnvironmentComponent", &scene.Environment(entity) });
        if (scene.HasLight(entity))
            result.push_back({ "Kairo.Engine.LightComponent", &scene.Light(entity) });
        if (scene.HasMeshRenderer(entity))
            result.push_back({ "Kairo.Engine.MeshRendererComponent", &scene.MeshRenderer(entity) });
        if (scene.HasLogic(entity))
            result.push_back({ "Kairo.Engine.LogicComponent", &scene.Logic(entity) });
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
        else if (typeKey == "Kairo.Engine.EnvironmentComponent")
            static_cast<const EnvironmentComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.LightComponent")
            static_cast<const LightComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.MeshRendererComponent")
            static_cast<const MeshRendererComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.LogicComponent")
            static_cast<const LogicComponent*>(object)->Validate();
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
                    PropertyFlags::None, std::nullopt, 0u }, &CameraComponent::Primary),
                MakeMemberProperty<CameraComponent>({ "orthographic-size", "Orthographic Size", "Lens", "Vertical world-space size of an orthographic view",
                    PropertyFlags::None, NumericRange{ 0.000001, 1'000'000.0, 0.05 }, 0u }, &CameraComponent::OrthographicSize),
                MakeMemberProperty<CameraComponent>({ "exposure-ev100", "Exposure EV100", "Exposure", "Camera exposure compensation in stops",
                    PropertyFlags::None, NumericRange{ -32.0, 32.0, 0.1 }, 0u }, &CameraComponent::ExposureEV100),
                MakeMemberProperty<CameraComponent>({ "render-layers", "Render Layers", "Culling", "64-bit mask of visible render layers",
                    PropertyFlags::Advanced, std::nullopt, 0u }, &CameraComponent::RenderLayers)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.EnvironmentComponent",
            .DisplayName = "Environment",
            .Category = "Rendering",
            .Properties = {
                MakeMemberProperty<EnvironmentComponent>({ "enabled", "Enabled", "General", "Allows this environment to participate in deterministic priority selection",
                    PropertyFlags::None, std::nullopt, 0u }, &EnvironmentComponent::Enabled),
                MakeMemberProperty<EnvironmentComponent>({ "priority", "Priority", "General", "Higher values win global environment selection",
                    PropertyFlags::None, std::nullopt, 0u }, &EnvironmentComponent::Priority),
                MakeMemberProperty<EnvironmentComponent>({ "ambient-intensity", "Ambient Intensity", "Lighting", "Non-negative ambient irradiance multiplier",
                    PropertyFlags::None, NumericRange{ 0.0, 1'000'000.0, 0.01 }, 0u }, &EnvironmentComponent::AmbientIntensity),
                MakeMemberProperty<EnvironmentComponent>({ "environment-intensity", "Environment Intensity", "Lighting", "Non-negative image-based lighting multiplier",
                    PropertyFlags::None, NumericRange{ 0.0, 1'000'000.0, 0.01 }, 0u }, &EnvironmentComponent::EnvironmentIntensity),
                MakeMemberProperty<EnvironmentComponent>({ "fog-density", "Fog Density", "Fog", "Exponential fog density",
                    PropertyFlags::None, NumericRange{ 0.0, 1'000.0, 0.001 }, 0u }, &EnvironmentComponent::FogDensity),
                MakeMemberProperty<EnvironmentComponent>({ "fog-near", "Fog Near", "Fog", "Linear fog start distance",
                    PropertyFlags::None, NumericRange{ 0.0, 10'000'000.0, 0.1 }, 0u }, &EnvironmentComponent::FogNear),
                MakeMemberProperty<EnvironmentComponent>({ "fog-far", "Fog Far", "Fog", "Linear fog end distance",
                    PropertyFlags::None, NumericRange{ 0.000001, 10'000'000.0, 1.0 }, 0u }, &EnvironmentComponent::FogFar),
                MakeMemberProperty<EnvironmentComponent>({ "exposure-ev100", "Exposure EV100", "Exposure", "Global exposure compensation in stops",
                    PropertyFlags::None, NumericRange{ -32.0, 32.0, 0.1 }, 0u }, &EnvironmentComponent::ExposureEV100)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.LightComponent",
            .DisplayName = "Light",
            .Category = "Rendering",
            .Properties = {
                MakeMemberProperty<LightComponent>({ "intensity", "Intensity", "Photometry", "Photometric intensity in the unit required by the selected light type",
                    PropertyFlags::None, NumericRange{ 0.0, 1'000'000'000.0, 1.0 }, 0u }, &LightComponent::Intensity),
                MakeMemberProperty<LightComponent>({ "range", "Range", "Shape", "Maximum influence distance for local lights",
                    PropertyFlags::None, NumericRange{ 0.000001, 10'000'000.0, 0.1 }, 0u }, &LightComponent::Range),
                MakeMemberProperty<LightComponent>({ "inner-cone-radians", "Inner Cone", "Shape", "Spot-light full-intensity half-angle in radians",
                    PropertyFlags::None, NumericRange{ 0.0, 1.56979633, 0.001 }, 0u }, &LightComponent::InnerConeRadians),
                MakeMemberProperty<LightComponent>({ "outer-cone-radians", "Outer Cone", "Shape", "Spot-light cutoff half-angle in radians",
                    PropertyFlags::None, NumericRange{ 0.000001, 1.56979633, 0.001 }, 0u }, &LightComponent::OuterConeRadians),
                MakeMemberProperty<LightComponent>({ "area-width", "Area Width", "Shape", "Rectangle width in local-space meters",
                    PropertyFlags::None, NumericRange{ 0.000001, 1'000'000.0, 0.01 }, 0u }, &LightComponent::AreaWidth),
                MakeMemberProperty<LightComponent>({ "area-height", "Area Height", "Shape", "Rectangle height in local-space meters",
                    PropertyFlags::None, NumericRange{ 0.000001, 1'000'000.0, 0.01 }, 0u }, &LightComponent::AreaHeight),
                MakeMemberProperty<LightComponent>({ "shadow-bias", "Shadow Bias", "Shadows", "Depth bias used by shadow adapters",
                    PropertyFlags::Advanced, NumericRange{ 0.0, 100.0, 0.0001 }, 0u }, &LightComponent::ShadowBias),
                MakeMemberProperty<LightComponent>({ "shadow-normal-bias", "Shadow Normal Bias", "Shadows", "Normal offset used by shadow adapters",
                    PropertyFlags::Advanced, NumericRange{ 0.0, 100.0, 0.0001 }, 0u }, &LightComponent::ShadowNormalBias),
                MakeMemberProperty<LightComponent>({ "render-layers", "Render Layers", "Culling", "64-bit mask of affected render layers",
                    PropertyFlags::Advanced, std::nullopt, 0u }, &LightComponent::RenderLayers)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.MeshRendererComponent",
            .DisplayName = "Mesh Renderer",
            .Category = "Rendering",
            .Properties = {
                MakeMemberProperty<MeshRendererComponent>({ "visible", "Visible", "General", "Controls render extraction visibility",
                    PropertyFlags::None, std::nullopt, 0u }, &MeshRendererComponent::Visible),
                MakeMemberProperty<MeshRendererComponent>({ "cast-shadows", "Cast Shadows", "Shadows", "Allows this renderer to contribute to shadow passes",
                    PropertyFlags::None, std::nullopt, 0u }, &MeshRendererComponent::CastShadows),
                MakeMemberProperty<MeshRendererComponent>({ "receive-shadows", "Receive Shadows", "Shadows", "Allows this renderer to receive scene shadows",
                    PropertyFlags::None, std::nullopt, 0u }, &MeshRendererComponent::ReceiveShadows),
                MakeMemberProperty<MeshRendererComponent>({ "render-layers", "Render Layers", "Culling", "64-bit mask used by cameras and lights",
                    PropertyFlags::Advanced, std::nullopt, 0u }, &MeshRendererComponent::RenderLayers)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.LogicComponent",
            .DisplayName = "Logic",
            .Category = "Gameplay",
            .Properties = {
                MakeMemberProperty<LogicComponent>({ "enabled", "Enabled", "General",
                    "Allows the attached gameplay document to execute in play mode",
                    PropertyFlags::None, std::nullopt, 0u }, &LogicComponent::Enabled)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.RigidBodyComponent",
            .DisplayName = "Rigid Body",
            .Category = "Physics",
            .Properties = {
                MakeMemberProperty<RigidBodyComponent>({ "density", "Density", "Mass", "Mass density used for generated mass properties",
                    PropertyFlags::None, NumericRange{ 0.000001, 1'000'000.0, 0.05 }, 0u }, &RigidBodyComponent::Density),
                MakeMemberProperty<RigidBodyComponent>({ "gravity-scale", "Gravity Scale", "Motion", "Multiplier applied to world gravity",
                    PropertyFlags::None, NumericRange{ -1000.0, 1000.0, 0.05 }, 0u }, &RigidBodyComponent::GravityScale),
                MakeMemberProperty<RigidBodyComponent>({ "linear-damping", "Linear Damping", "Motion", "Non-negative linear velocity damping",
                    PropertyFlags::None, NumericRange{ 0.0, 1000.0, 0.01 }, 0u }, &RigidBodyComponent::LinearDamping),
                MakeMemberProperty<RigidBodyComponent>({ "angular-damping", "Angular Damping", "Motion", "Non-negative angular velocity damping",
                    PropertyFlags::None, NumericRange{ 0.0, 1000.0, 0.01 }, 0u }, &RigidBodyComponent::AngularDamping)
            }
        });

        registry.Register({
            .Key = "Kairo.Engine.ColliderComponent",
            .DisplayName = "Collider",
            .Category = "Physics",
            .Properties = {
                MakeMemberProperty<ColliderComponent>({ "radius", "Radius", "Shape", "Sphere or capsule radius",
                    PropertyFlags::None, NumericRange{ 0.000001, 1'000'000.0, 0.01 }, 0u }, &ColliderComponent::Radius),
                MakeMemberProperty<ColliderComponent>({ "half-height", "Half Height", "Shape", "Capsule segment half-height",
                    PropertyFlags::None, NumericRange{ 0.000001, 1'000'000.0, 0.01 }, 0u }, &ColliderComponent::HalfHeight),
                MakeMemberProperty<ColliderComponent>({ "friction", "Friction", "Material", "Non-negative Coulomb friction coefficient",
                    PropertyFlags::None, NumericRange{ 0.0, 1000.0, 0.01 }, 0u }, &ColliderComponent::Friction),
                MakeMemberProperty<ColliderComponent>({ "restitution", "Restitution", "Material", "Bounciness in the inclusive range [0, 1]",
                    PropertyFlags::None, NumericRange{ 0.0, 1.0, 0.01 }, 0u }, &ColliderComponent::Restitution),
                MakeMemberProperty<ColliderComponent>({ "belongs-to", "Category Mask", "Filtering", "Bit mask describing this collider's categories",
                    PropertyFlags::Advanced, std::nullopt, 0u }, &ColliderComponent::BelongsTo),
                MakeMemberProperty<ColliderComponent>({ "collides-with", "Collision Mask", "Filtering", "Bit mask of categories this collider accepts",
                    PropertyFlags::Advanced, std::nullopt, 0u }, &ColliderComponent::CollidesWith),
                MakeMemberProperty<ColliderComponent>({ "is-trigger", "Is Trigger", "Collision", "Reports overlap events without contact response",
                    PropertyFlags::None, std::nullopt, 0u }, &ColliderComponent::IsTrigger)
            }
        });
    }
}
