module;

#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.EngineCore.Reflection;

import Kairo.Assets;
import Kairo.EngineCore.Components;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.RuntimeComponents;
import Kairo.EngineCore.Scene;
import Kairo.Foundation.Math;
import Kairo.Reflection;

export namespace kairo::engine
{
    namespace reflection_detail
    {
        using namespace kairo::foundation::math;
        using namespace kairo::reflection;

        [[nodiscard]] inline float CheckedFloat(double value)
        {
            if (!std::isfinite(value) || value < -static_cast<double>(std::numeric_limits<float>::max()) ||
                value > static_cast<double>(std::numeric_limits<float>::max()))
                throw std::out_of_range("Reflected composite component does not fit a finite float.");
            return static_cast<float>(value);
        }

        [[nodiscard]] inline PropertyValue EncodeVec3(const Vec3f& value)
        {
            return PropertyValue(Vector3Value{ value.x, value.y, value.z });
        }

        [[nodiscard]] inline Vec3f DecodeVec3(const PropertyValue& value)
        {
            const Vector3Value& source = value.Get<Vector3Value>();
            return { CheckedFloat(source.X), CheckedFloat(source.Y), CheckedFloat(source.Z) };
        }

        [[nodiscard]] inline PropertyValue EncodeVec4(const Vec4f& value)
        {
            return PropertyValue(Vector4Value{ value.x, value.y, value.z, value.w });
        }

        [[nodiscard]] inline Vec4f DecodeVec4(const PropertyValue& value)
        {
            const Vector4Value& source = value.Get<Vector4Value>();
            return { CheckedFloat(source.X), CheckedFloat(source.Y), CheckedFloat(source.Z), CheckedFloat(source.W) };
        }

        [[nodiscard]] inline PropertyValue EncodeQuaternion(const Quatf& value)
        {
            return PropertyValue(QuaternionValue{ value.x, value.y, value.z, value.w });
        }

        [[nodiscard]] inline Quatf DecodeQuaternion(const PropertyValue& value)
        {
            const QuaternionValue& source = value.Get<QuaternionValue>();
            const double lengthSquared = source.X * source.X + source.Y * source.Y +
                source.Z * source.Z + source.W * source.W;
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-20)
                throw std::invalid_argument("Reflected transform rotation must be a non-zero finite quaternion.");
            return Quatf{
                CheckedFloat(source.X), CheckedFloat(source.Y),
                CheckedFloat(source.Z), CheckedFloat(source.W)
            }.Normalized();
        }

        [[nodiscard]] inline PropertyDescriptor MakeTransformVector3Property(
            PropertyMetadata metadata,
            Vec3f Transformf::* member)
        {
            PropertyDescriptor descriptor;
            descriptor.Metadata = std::move(metadata);
            descriptor.ValueKind = PropertyValueKind::Vector3;
            descriptor.Read = [member](const void* object)
            {
                return EncodeVec3(static_cast<const TransformComponent*>(object)->Local.*member);
            };
            if (!HasFlag(descriptor.Metadata.Flags, PropertyFlags::ReadOnly))
            {
                descriptor.Write = [member](void* object, const PropertyValue& value)
                {
                    static_cast<TransformComponent*>(object)->Local.*member = DecodeVec3(value);
                };
            }
            return descriptor;
        }

        [[nodiscard]] inline PropertyDescriptor MakeTransformQuaternionProperty(
            PropertyMetadata metadata,
            Quatf Transformf::* member)
        {
            PropertyDescriptor descriptor;
            descriptor.Metadata = std::move(metadata);
            descriptor.ValueKind = PropertyValueKind::Quaternion;
            descriptor.Read = [member](const void* object)
            {
                return EncodeQuaternion(static_cast<const TransformComponent*>(object)->Local.*member);
            };
            if (!HasFlag(descriptor.Metadata.Flags, PropertyFlags::ReadOnly))
            {
                descriptor.Write = [member](void* object, const PropertyValue& value)
                {
                    static_cast<TransformComponent*>(object)->Local.*member = DecodeQuaternion(value);
                };
            }
            return descriptor;
        }

        template<typename Object>
        [[nodiscard]] PropertyDescriptor MakeVec3MemberProperty(
            PropertyMetadata metadata,
            Vec3f Object::* member)
        {
            return MakeAdaptedMemberProperty<Object, Vec3f>(
                std::move(metadata), member, PropertyValueKind::Vector3,
                [](const Vec3f& value) { return EncodeVec3(value); },
                [](const PropertyValue& value) { return DecodeVec3(value); });
        }

        template<typename Object>
        [[nodiscard]] PropertyDescriptor MakeVec4MemberProperty(
            PropertyMetadata metadata,
            Vec4f Object::* member)
        {
            return MakeAdaptedMemberProperty<Object, Vec4f>(
                std::move(metadata), member, PropertyValueKind::Vector4,
                [](const Vec4f& value) { return EncodeVec4(value); },
                [](const PropertyValue& value) { return DecodeVec4(value); });
        }

        template<typename Object, typename Handle>
        [[nodiscard]] PropertyDescriptor MakeAssetHandleProperty(
            PropertyMetadata metadata,
            Handle Object::* member,
            std::string targetType)
        {
            return MakeReferenceMemberProperty<Object, Handle>(
                std::move(metadata), member, std::move(targetType),
                [](const Handle& handle)
                {
                    return handle.IsValid() ? handle.ID.ToString() : std::string{};
                },
                [](std::string_view identifier)
                {
                    Handle handle{};
                    if (!identifier.empty()) handle.ID = kairo::assets::AssetID::Parse(identifier);
                    return handle;
                });
        }

        [[nodiscard]] inline std::vector<EnumOption> CameraProjectionOptions()
        {
            return {
                { static_cast<std::int64_t>(CameraProjection::Perspective), "Kairo.Engine.CameraProjection.Perspective", "Perspective" },
                { static_cast<std::int64_t>(CameraProjection::Orthographic), "Kairo.Engine.CameraProjection.Orthographic", "Orthographic" }
            };
        }

        [[nodiscard]] inline std::vector<EnumOption> CameraClearModeOptions()
        {
            return {
                { static_cast<std::int64_t>(CameraClearMode::Environment), "Kairo.Engine.CameraClearMode.Environment", "Environment" },
                { static_cast<std::int64_t>(CameraClearMode::SolidColor), "Kairo.Engine.CameraClearMode.SolidColor", "Solid Color" },
                { static_cast<std::int64_t>(CameraClearMode::DepthOnly), "Kairo.Engine.CameraClearMode.DepthOnly", "Depth Only" },
                { static_cast<std::int64_t>(CameraClearMode::Nothing), "Kairo.Engine.CameraClearMode.Nothing", "Nothing" }
            };
        }

        [[nodiscard]] inline std::vector<EnumOption> FogModeOptions()
        {
            return {
                { static_cast<std::int64_t>(FogMode::Disabled), "Kairo.Engine.FogMode.Disabled", "Disabled" },
                { static_cast<std::int64_t>(FogMode::Linear), "Kairo.Engine.FogMode.Linear", "Linear" },
                { static_cast<std::int64_t>(FogMode::Exponential), "Kairo.Engine.FogMode.Exponential", "Exponential" }
            };
        }

        [[nodiscard]] inline std::vector<EnumOption> ToneMappingOptions()
        {
            return {
                { static_cast<std::int64_t>(ToneMapping::None), "Kairo.Engine.ToneMapping.None", "None" },
                { static_cast<std::int64_t>(ToneMapping::Reinhard), "Kairo.Engine.ToneMapping.Reinhard", "Reinhard" },
                { static_cast<std::int64_t>(ToneMapping::ACES), "Kairo.Engine.ToneMapping.ACES", "ACES" }
            };
        }

        [[nodiscard]] inline std::vector<EnumOption> ShadowPolicyOptions()
        {
            return {
                { static_cast<std::int64_t>(ShadowPolicy::Disabled), "Kairo.Engine.ShadowPolicy.Disabled", "Disabled" },
                { static_cast<std::int64_t>(ShadowPolicy::Hard), "Kairo.Engine.ShadowPolicy.Hard", "Hard" },
                { static_cast<std::int64_t>(ShadowPolicy::Soft), "Kairo.Engine.ShadowPolicy.Soft", "Soft" }
            };
        }

        [[nodiscard]] inline std::vector<EnumOption> RigidBodyMotionOptions()
        {
            return {
                { static_cast<std::int64_t>(RigidBodyMotion::Static), "Kairo.Engine.RigidBodyMotion.Static", "Static" },
                { static_cast<std::int64_t>(RigidBodyMotion::Dynamic), "Kairo.Engine.RigidBodyMotion.Dynamic", "Dynamic" },
                { static_cast<std::int64_t>(RigidBodyMotion::Kinematic), "Kairo.Engine.RigidBodyMotion.Kinematic", "Kinematic" }
            };
        }

        [[nodiscard]] inline std::vector<EnumOption> ColliderShapeOptions()
        {
            return {
                { static_cast<std::int64_t>(ColliderShape::Box), "Kairo.Engine.ColliderShape.Box", "Box" },
                { static_cast<std::int64_t>(ColliderShape::Sphere), "Kairo.Engine.ColliderShape.Sphere", "Sphere" },
                { static_cast<std::int64_t>(ColliderShape::Capsule), "Kairo.Engine.ColliderShape.Capsule", "Capsule" }
            };
        }

        inline void ValidateTransform(const TransformComponent& transform)
        {
            const auto finite3 = [](const Vec3f& value)
            {
                return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
            };
            const Quatf& rotation = transform.Local.Rotation;
            if (!finite3(transform.Local.Translation) || !finite3(transform.Local.Scale) ||
                !std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
                !std::isfinite(rotation.z) || !std::isfinite(rotation.w) ||
                rotation.LengthSquared() <= 1.0e-12f)
                throw std::invalid_argument("TransformComponent contains non-finite TRS data or a zero rotation quaternion.");
        }
    }

    /// A transient link between a Scene-owned component and its stable
    /// reflection type key. The pointer remains non-owning and must be resolved
    /// again after structural scene mutation.
    struct ReflectedSceneComponent final
    {
        std::string_view TypeKey;
        void* Object = nullptr;
    };

    [[nodiscard]] inline std::vector<ReflectedSceneComponent> EnumerateReflectedComponents(
        Scene& scene, Entity entity)
    {
        std::vector<ReflectedSceneComponent> result;
        result.reserve(9u);
        result.push_back({ "Kairo.Engine.NameComponent", &scene.Name(entity) });
        result.push_back({ "Kairo.Engine.TransformComponent", &scene.Transform(entity) });
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

    [[nodiscard]] inline void* ResolveReflectedComponent(Scene& scene, Entity entity,
        std::string_view typeKey)
    {
        for (const ReflectedSceneComponent component : EnumerateReflectedComponents(scene, entity))
            if (component.TypeKey == typeKey) return component.Object;
        throw std::logic_error("Entity does not expose reflected component: " + std::string(typeKey));
    }

    inline void ValidateReflectedComponent(std::string_view typeKey, const void* object)
    {
        if (object == nullptr)
            throw std::invalid_argument("Reflected component validation requires a non-null object.");
        if (typeKey == "Kairo.Engine.TransformComponent")
            reflection_detail::ValidateTransform(*static_cast<const TransformComponent*>(object));
        else if (typeKey == "Kairo.Engine.CameraComponent")
            static_cast<const CameraComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.EnvironmentComponent")
            static_cast<const EnvironmentComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.LightComponent")
            static_cast<const LightComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.MeshRendererComponent")
            static_cast<const MeshRendererComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.LogicComponent")
            static_cast<const LogicComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.RigidBodyComponent")
            static_cast<const RigidBodyComponent*>(object)->Validate();
        else if (typeKey == "Kairo.Engine.ColliderComponent")
            static_cast<const ColliderComponent*>(object)->Validate();
    }

    inline void RegisterEngineCoreReflection(kairo::reflection::ReflectionRegistry& registry)
    {
        using namespace kairo::reflection;
        using namespace reflection_detail;

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
            .Key = "Kairo.Engine.TransformComponent",
            .DisplayName = "Transform",
            .Category = "Core",
            .Properties = {
                MakeTransformVector3Property({ .Key = "translation", .DisplayName = "Translation", .Category = "Transform",
                    .Tooltip = "Local-space translation" }, &kairo::foundation::math::Transformf::Translation),
                MakeTransformQuaternionProperty({ .Key = "rotation", .DisplayName = "Rotation", .Category = "Transform",
                    .Tooltip = "Normalized local-space quaternion rotation" }, &kairo::foundation::math::Transformf::Rotation),
                MakeTransformVector3Property({ .Key = "scale", .DisplayName = "Scale", .Category = "Transform",
                    .Tooltip = "Local-space non-uniform scale" }, &kairo::foundation::math::Transformf::Scale)
            }
        });

        PropertyMetadata cameraProjection{
            .Key = "projection", .DisplayName = "Projection", .Category = "Lens",
            .Tooltip = "Perspective or orthographic projection mode",
            .EnumOptions = CameraProjectionOptions()
        };
        PropertyMetadata cameraClearMode{
            .Key = "clear-mode", .DisplayName = "Clear Mode", .Category = "Clear",
            .Tooltip = "Framebuffer clear policy for this camera",
            .EnumOptions = CameraClearModeOptions()
        };
        registry.Register({
            .Key = "Kairo.Engine.CameraComponent",
            .DisplayName = "Camera",
            .Category = "Rendering",
            .Properties = {
                MakeEnumMemberProperty<CameraComponent>(std::move(cameraProjection), &CameraComponent::Projection),
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
                MakeEnumMemberProperty<CameraComponent>(std::move(cameraClearMode), &CameraComponent::ClearMode),
                MakeVec4MemberProperty<CameraComponent>({ .Key = "clear-color", .DisplayName = "Clear Color", .Category = "Clear",
                    .Tooltip = "Linear RGBA clear color" }, &CameraComponent::ClearColor),
                MakeMemberProperty<CameraComponent>({ "render-layers", "Render Layers", "Culling", "64-bit mask of visible render layers",
                    PropertyFlags::Advanced, std::nullopt, 0u }, &CameraComponent::RenderLayers)
            }
        });

        PropertyMetadata fogMode{
            .Key = "fog-mode", .DisplayName = "Fog Mode", .Category = "Fog",
            .Tooltip = "Environment fog equation",
            .EnumOptions = FogModeOptions()
        };
        PropertyMetadata toneMap{
            .Key = "tone-mapping", .DisplayName = "Tone Mapping", .Category = "Exposure",
            .Tooltip = "Global tone-mapping operator",
            .EnumOptions = ToneMappingOptions()
        };
        registry.Register({
            .Key = "Kairo.Engine.EnvironmentComponent",
            .DisplayName = "Environment",
            .Category = "Rendering",
            .Properties = {
                MakeMemberProperty<EnvironmentComponent>({ "enabled", "Enabled", "General", "Allows this environment to participate in deterministic priority selection",
                    PropertyFlags::None, std::nullopt, 0u }, &EnvironmentComponent::Enabled),
                MakeMemberProperty<EnvironmentComponent>({ "priority", "Priority", "General", "Higher values win global environment selection",
                    PropertyFlags::None, std::nullopt, 0u }, &EnvironmentComponent::Priority),
                MakeVec3MemberProperty<EnvironmentComponent>({ .Key = "background-color", .DisplayName = "Background Color", .Category = "Lighting",
                    .Tooltip = "Linear RGB background color" }, &EnvironmentComponent::BackgroundColor),
                MakeMemberProperty<EnvironmentComponent>({ "ambient-intensity", "Ambient Intensity", "Lighting", "Non-negative ambient irradiance multiplier",
                    PropertyFlags::None, NumericRange{ 0.0, 1'000'000.0, 0.01 }, 0u }, &EnvironmentComponent::AmbientIntensity),
                MakeMemberProperty<EnvironmentComponent>({ "environment-intensity", "Environment Intensity", "Lighting", "Non-negative image-based lighting multiplier",
                    PropertyFlags::None, NumericRange{ 0.0, 1'000'000.0, 0.01 }, 0u }, &EnvironmentComponent::EnvironmentIntensity),
                MakeEnumMemberProperty<EnvironmentComponent>(std::move(fogMode), &EnvironmentComponent::Fog),
                MakeVec3MemberProperty<EnvironmentComponent>({ .Key = "fog-color", .DisplayName = "Fog Color", .Category = "Fog",
                    .Tooltip = "Linear RGB fog color" }, &EnvironmentComponent::FogColor),
                MakeMemberProperty<EnvironmentComponent>({ "fog-density", "Fog Density", "Fog", "Exponential fog density",
                    PropertyFlags::None, NumericRange{ 0.0, 1'000.0, 0.001 }, 0u }, &EnvironmentComponent::FogDensity),
                MakeMemberProperty<EnvironmentComponent>({ "fog-near", "Fog Near", "Fog", "Linear fog start distance",
                    PropertyFlags::None, NumericRange{ 0.0, 10'000'000.0, 0.1 }, 0u }, &EnvironmentComponent::FogNear),
                MakeMemberProperty<EnvironmentComponent>({ "fog-far", "Fog Far", "Fog", "Linear fog end distance",
                    PropertyFlags::None, NumericRange{ 0.000001, 10'000'000.0, 1.0 }, 0u }, &EnvironmentComponent::FogFar),
                MakeMemberProperty<EnvironmentComponent>({ "exposure-ev100", "Exposure EV100", "Exposure", "Global exposure compensation in stops",
                    PropertyFlags::None, NumericRange{ -32.0, 32.0, 0.1 }, 0u }, &EnvironmentComponent::ExposureEV100),
                MakeEnumMemberProperty<EnvironmentComponent>(std::move(toneMap), &EnvironmentComponent::ToneMap)
            }
        });

        PropertyMetadata shadowPolicy{
            .Key = "shadow-policy", .DisplayName = "Shadows", .Category = "Shadows",
            .Tooltip = "Shadow rendering policy",
            .EnumOptions = ShadowPolicyOptions()
        };
        registry.Register({
            .Key = "Kairo.Engine.LightComponent",
            .DisplayName = "Light",
            .Category = "Rendering",
            .Properties = {
                MakeVec3MemberProperty<LightComponent>({ .Key = "color", .DisplayName = "Color", .Category = "Photometry",
                    .Tooltip = "Linear RGB light color" }, &LightComponent::Color),
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
                MakeEnumMemberProperty<LightComponent>(std::move(shadowPolicy), &LightComponent::Shadows),
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
                MakeAssetHandleProperty<MeshRendererComponent>({ .Key = "mesh-asset", .DisplayName = "Mesh", .Category = "Assets",
                    .Tooltip = "Persistent mesh asset reference", .MaximumReferenceBytes = 36u },
                    &MeshRendererComponent::MeshAsset, "Kairo.Assets.Mesh"),
                MakeAssetHandleProperty<MeshRendererComponent>({ .Key = "material-asset", .DisplayName = "Material", .Category = "Assets",
                    .Tooltip = "Persistent primary material asset reference", .MaximumReferenceBytes = 36u },
                    &MeshRendererComponent::MaterialAsset, "Kairo.Assets.Material"),
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
                MakeAssetHandleProperty<LogicComponent>({ .Key = "document", .DisplayName = "Document", .Category = "Assets",
                    .Tooltip = "Persistent gameplay document reference", .MaximumReferenceBytes = 36u },
                    &LogicComponent::Document, "Kairo.Assets.Document"),
                MakeMemberProperty<LogicComponent>({ "enabled", "Enabled", "General",
                    "Allows the attached gameplay document to execute in play mode",
                    PropertyFlags::None, std::nullopt, 0u }, &LogicComponent::Enabled)
            }
        });

        PropertyMetadata rigidBodyMotion{
            .Key = "motion", .DisplayName = "Motion", .Category = "Motion",
            .Tooltip = "Static, dynamic, or kinematic body policy",
            .EnumOptions = RigidBodyMotionOptions()
        };
        registry.Register({
            .Key = "Kairo.Engine.RigidBodyComponent",
            .DisplayName = "Rigid Body",
            .Category = "Physics",
            .Properties = {
                MakeEnumMemberProperty<RigidBodyComponent>(std::move(rigidBodyMotion), &RigidBodyComponent::Motion),
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

        PropertyMetadata colliderShape{
            .Key = "shape", .DisplayName = "Shape", .Category = "Shape",
            .Tooltip = "Primitive collider shape",
            .EnumOptions = ColliderShapeOptions()
        };
        registry.Register({
            .Key = "Kairo.Engine.ColliderComponent",
            .DisplayName = "Collider",
            .Category = "Physics",
            .Properties = {
                MakeEnumMemberProperty<ColliderComponent>(std::move(colliderShape), &ColliderComponent::Shape),
                MakeVec3MemberProperty<ColliderComponent>({ .Key = "half-extents", .DisplayName = "Half Extents", .Category = "Shape",
                    .Tooltip = "Box half extents in local space" }, &ColliderComponent::HalfExtents),
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
