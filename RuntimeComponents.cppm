module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
export module Kairo.EngineCore.RuntimeComponents;

import Kairo.Assets;
import Kairo.Foundation.Math.Vector;

export namespace kairo::engine
{
    inline constexpr std::uint32_t MaximumSceneLayer = 63u;
    inline constexpr std::size_t MaximumEntityTags = 32u;
    inline constexpr std::size_t MaximumEntityTagBytes = 64u;
    inline constexpr std::size_t MaximumMaterialSlots = 256u;
    inline constexpr std::uint64_t AllRenderLayers =
        std::numeric_limits<std::uint64_t>::max();

    namespace rendering_component_detail
    {
        [[nodiscard]] inline bool Finite(float value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] inline bool Finite(
            const kairo::foundation::math::Vec3f& value) noexcept
        {
            return Finite(value.x) && Finite(value.y) && Finite(value.z);
        }

        [[nodiscard]] inline bool Finite(
            const kairo::foundation::math::Vec4f& value) noexcept
        {
            return Finite(value.x) && Finite(value.y) && Finite(value.z) && Finite(value.w);
        }

        [[nodiscard]] inline bool NonNegative(
            const kairo::foundation::math::Vec3f& value) noexcept
        {
            return value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
        }
    }

    /// Authored entity-wide behavior shared by editor and runtime systems.
    /// Layer zero is the default; layers are bounded to 0..63 so masks fit one
    /// portable 64-bit value. Tags are sorted for deterministic persistence.
    struct EntitySettingsComponent final
    {
        bool Enabled = true;
        std::uint32_t Layer = 0u;
        std::vector<std::string> Tags;

        static void ValidateTag(std::string_view tag)
        {
            if (tag.empty() || tag.size() > MaximumEntityTagBytes)
                throw std::invalid_argument("Entity tags must contain between 1 and 64 bytes.");
            if (std::ranges::any_of(tag, [](unsigned char character) {
                return character < 0x20u || character == 0x7fu; }))
                throw std::invalid_argument("Entity tags cannot contain ASCII control characters.");
        }

        void Validate() const
        {
            if (Layer > MaximumSceneLayer)
                throw std::invalid_argument("Entity layer must be between 0 and 63.");
            if (Tags.size() > MaximumEntityTags)
                throw std::length_error("Entity exceeds its 32-tag safety limit.");
            for (std::size_t index = 0u; index < Tags.size(); ++index)
            {
                ValidateTag(Tags[index]);
                if (index > 0u && Tags[index - 1u] >= Tags[index])
                    throw std::invalid_argument("Entity tags must be unique and sorted.");
            }
        }
    };

    /// Persistent typed handles keep scene serialization independent from a
    /// concrete loader while preserving references across asset path moves.
    struct MeshRendererComponent final
    {
        kairo::assets::MeshAssetHandle MeshAsset;
        kairo::assets::MaterialAssetHandle MaterialAsset;
        bool Visible = true;
        std::vector<kairo::assets::MaterialAssetHandle> AdditionalMaterialSlots;
        bool CastShadows = true;
        bool ReceiveShadows = true;
        std::uint64_t RenderLayers = AllRenderLayers;

        /// Output: the number of ordered submesh material bindings. Slot zero
        /// is the original MaterialAsset field; later slots live in
        /// AdditionalMaterialSlots to preserve V2 source compatibility.
        [[nodiscard]] std::size_t MaterialSlotCount() const noexcept
        {
            return 1u + AdditionalMaterialSlots.size();
        }

        /// Input: a zero-based submesh material slot.
        /// Output: the persistent material handle assigned to that slot.
        /// Degeneracy: an out-of-range slot throws rather than falling back to
        /// another material and hiding an importer/cook mismatch.
        [[nodiscard]] kairo::assets::MaterialAssetHandle MaterialForSlot(
            std::size_t slot) const
        {
            if (slot == 0u) return MaterialAsset;
            if (slot > AdditionalMaterialSlots.size())
                throw std::out_of_range("Mesh renderer material slot is out of range.");
            return AdditionalMaterialSlots[slot - 1u];
        }

        /// Task: validate the renderer-independent mesh/material references.
        /// Input: valid persistent KairoAssets handles of the required types.
        /// Output: no value; throws std::invalid_argument on an unusable component.
        /// This deliberately does not load assets: loading belongs to an adapter
        /// such as KairoRenderer, keeping EngineCore usable in headless tools.
        void Validate() const
        {
            if (!MeshAsset.IsValid() || !MaterialAsset.IsValid())
                throw std::invalid_argument("MeshRendererComponent requires valid mesh and material asset handles.");
            if (MaterialSlotCount() > MaximumMaterialSlots)
                throw std::length_error("MeshRendererComponent exceeds its 256 material-slot safety limit.");
            for (const auto material : AdditionalMaterialSlots)
                if (!material.IsValid())
                    throw std::invalid_argument("MeshRendererComponent material slots require valid asset handles.");
            if (RenderLayers == 0u)
                throw std::invalid_argument("MeshRendererComponent render layer mask cannot be empty.");
        }
    };

    enum class CameraProjection : std::uint8_t { Perspective, Orthographic };
    enum class CameraClearMode : std::uint8_t { Environment, SolidColor, DepthOnly, Nothing };

    struct CameraComponent final
    {
        float VerticalFovRadians = 1.0471975512f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        bool Primary = false;
        CameraProjection Projection = CameraProjection::Perspective;
        float OrthographicSize = 10.0f;
        float ExposureEV100 = 0.0f;
        CameraClearMode ClearMode = CameraClearMode::Environment;
        kairo::foundation::math::Vec4f ClearColor{ 0.02f, 0.025f, 0.035f, 1.0f };
        std::uint64_t RenderLayers = AllRenderLayers;

        /// Task: validate perspective parameters before a renderer derives a matrix.
        /// Input: vertical field of view in radians and positive near/far planes.
        /// Output: no value; throws std::invalid_argument for a non-projectable frustum.
        /// Convention: right-handed applications should use the same camera-space
        /// convention as their renderer; EngineCore stores no renderer-specific matrix.
        void Validate() const
        {
            using namespace rendering_component_detail;
            if (!Finite(VerticalFovRadians) ||
                !(VerticalFovRadians > 0.0f && VerticalFovRadians < 3.14159265f) ||
                !Finite(OrthographicSize) || OrthographicSize <= 0.0f ||
                !Finite(NearPlane) || !Finite(FarPlane) || NearPlane <= 0.0f ||
                FarPlane <= NearPlane || !Finite(ExposureEV100))
                throw std::invalid_argument("CameraComponent has an invalid projection range.");
            switch (Projection)
            {
                case CameraProjection::Perspective:
                case CameraProjection::Orthographic: break;
                default: throw std::invalid_argument("CameraComponent projection is unsupported.");
            }
            switch (ClearMode)
            {
                case CameraClearMode::Environment:
                case CameraClearMode::SolidColor:
                case CameraClearMode::DepthOnly:
                case CameraClearMode::Nothing: break;
                default: throw std::invalid_argument("CameraComponent clear mode is unsupported.");
            }
            if (!Finite(ClearColor) || !NonNegative({ ClearColor.x, ClearColor.y, ClearColor.z }) ||
                ClearColor.w < 0.0f || ClearColor.w > 1.0f)
                throw std::invalid_argument("CameraComponent clear color must be finite, non-negative, and have normalized alpha.");
            if (RenderLayers == 0u)
                throw std::invalid_argument("CameraComponent render layer mask cannot be empty.");
        }
    };

    enum class LightType : std::uint8_t { Directional, Point, Spot, RectangleArea };
    enum class PhotometricUnit : std::uint8_t { Lux, Candela, Nit };
    enum class ShadowPolicy : std::uint8_t { Disabled, Hard, Soft };

    /// Renderer-neutral authored light descriptor.
    ///
    /// Input: linear RGB chromaticity, a non-negative photometric intensity,
    /// and type-specific geometric parameters. Output: validated scene data
    /// that real-time and offline adapters can interpret identically.
    /// Conventions: directional lights use lux; point and spot lights use
    /// candela; rectangular emitters use nits (cd/m^2). Local -Z is the light
    /// forward direction and a rectangle lies in its local XY plane.
    struct LightComponent final
    {
        LightType Type = LightType::Directional;
        kairo::foundation::math::Vec3f Color{ 1.0f, 1.0f, 1.0f };
        float Intensity = 100'000.0f;
        PhotometricUnit Unit = PhotometricUnit::Lux;
        float Range = 10.0f;
        float InnerConeRadians = 0.34906585f;
        float OuterConeRadians = 0.52359878f;
        float AreaWidth = 1.0f;
        float AreaHeight = 1.0f;
        ShadowPolicy Shadows = ShadowPolicy::Soft;
        float ShadowBias = 0.001f;
        float ShadowNormalBias = 0.01f;
        std::uint64_t RenderLayers = AllRenderLayers;

        void Validate() const
        {
            using namespace rendering_component_detail;
            if (!Finite(Color) || !NonNegative(Color) || !Finite(Intensity) || Intensity < 0.0f)
                throw std::invalid_argument("Light color and intensity must be finite and non-negative.");
            if (!Finite(Range) || Range <= 0.0f || !Finite(AreaWidth) || AreaWidth <= 0.0f ||
                !Finite(AreaHeight) || AreaHeight <= 0.0f)
                throw std::invalid_argument("Light range and area dimensions must be finite and positive.");
            if (!Finite(InnerConeRadians) || !Finite(OuterConeRadians) ||
                InnerConeRadians < 0.0f || OuterConeRadians <= 0.0f ||
                InnerConeRadians > OuterConeRadians || OuterConeRadians >= 1.57079633f)
                throw std::invalid_argument("Spot-light cone angles must satisfy 0 <= inner <= outer < pi/2.");
            if (!Finite(ShadowBias) || ShadowBias < 0.0f ||
                !Finite(ShadowNormalBias) || ShadowNormalBias < 0.0f)
                throw std::invalid_argument("Light shadow biases must be finite and non-negative.");
            const bool unitMatches =
                (Type == LightType::Directional && Unit == PhotometricUnit::Lux) ||
                ((Type == LightType::Point || Type == LightType::Spot) &&
                    Unit == PhotometricUnit::Candela) ||
                (Type == LightType::RectangleArea && Unit == PhotometricUnit::Nit);
            if (!unitMatches)
                throw std::invalid_argument("Light photometric unit does not match its light type.");
            switch (Shadows)
            {
                case ShadowPolicy::Disabled:
                case ShadowPolicy::Hard:
                case ShadowPolicy::Soft: break;
                default: throw std::invalid_argument("Light shadow policy is unsupported.");
            }
            if (RenderLayers == 0u)
                throw std::invalid_argument("Light render layer mask cannot be empty.");
        }
    };

    enum class FogMode : std::uint8_t { Disabled, Linear, Exponential };
    enum class ToneMapping : std::uint8_t { None, Reinhard, ACES };

    /// Global environment candidate attached to a scene entity. Multiple
    /// candidates are allowed for future volume/blending work; Scene selects
    /// the enabled highest-priority component deterministically.
    struct EnvironmentComponent final
    {
        bool Enabled = true;
        std::int32_t Priority = 0;
        kairo::foundation::math::Vec3f BackgroundColor{ 0.02f, 0.025f, 0.035f };
        std::optional<kairo::assets::TextureAssetHandle> EnvironmentTexture;
        float AmbientIntensity = 0.0f;
        float EnvironmentIntensity = 1.0f;
        FogMode Fog = FogMode::Disabled;
        kairo::foundation::math::Vec3f FogColor{ 0.5f, 0.5f, 0.5f };
        float FogDensity = 0.01f;
        float FogNear = 0.0f;
        float FogFar = 1000.0f;
        float ExposureEV100 = 0.0f;
        ToneMapping ToneMap = ToneMapping::ACES;

        void Validate() const
        {
            using namespace rendering_component_detail;
            if (!Finite(BackgroundColor) || !NonNegative(BackgroundColor) ||
                !Finite(FogColor) || !NonNegative(FogColor))
                throw std::invalid_argument("Environment colors must be finite and non-negative linear RGB.");
            if (EnvironmentTexture.has_value() && !EnvironmentTexture->IsValid())
                throw std::invalid_argument("Environment texture handle must be valid when present.");
            if (!Finite(AmbientIntensity) || AmbientIntensity < 0.0f ||
                !Finite(EnvironmentIntensity) || EnvironmentIntensity < 0.0f ||
                !Finite(ExposureEV100))
                throw std::invalid_argument("Environment intensities and exposure must be finite and non-negative where applicable.");
            if (!Finite(FogDensity) || FogDensity < 0.0f || !Finite(FogNear) ||
                !Finite(FogFar) || FogNear < 0.0f || FogFar <= FogNear)
                throw std::invalid_argument("Environment fog range/density is invalid.");
            switch (Fog)
            {
                case FogMode::Disabled:
                case FogMode::Linear:
                case FogMode::Exponential: break;
                default: throw std::invalid_argument("Environment fog mode is unsupported.");
            }
            switch (ToneMap)
            {
                case ToneMapping::None:
                case ToneMapping::Reinhard:
                case ToneMapping::ACES: break;
                default: throw std::invalid_argument("Environment tone mapping is unsupported.");
            }
        }
    };

    /// Persistent gameplay-logic attachment. The document asset is authored
    /// once and may later compile to platform-specific bytecode; scenes retain
    /// the source identity so editor, build pipeline, and runtime diagnostics
    /// can agree on ownership. Runtime VM instances are process-local and are
    /// deliberately not serialized here.
    struct LogicComponent final
    {
        kairo::assets::DocumentAssetHandle Document;
        bool Enabled = true;

        void Validate() const
        {
            if (!Document.IsValid())
                throw std::invalid_argument("LogicComponent requires a valid document asset handle.");
        }
    };

    enum class RigidBodyMotion : std::uint8_t { Static, Dynamic, Kinematic };
    enum class ColliderShape : std::uint8_t { Box, Sphere, Capsule };

    /// Persistent physics authoring data. Runtime body IDs belong to the world
    /// adapter and are deliberately absent from scenes.
    struct RigidBodyComponent final
    {
        RigidBodyMotion Motion = RigidBodyMotion::Dynamic;
        float Density = 1.0f;
        float GravityScale = 1.0f;
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;

        void Validate() const
        {
            if (!std::isfinite(Density) || Density <= 0.0f)
                throw std::invalid_argument("Rigid body density must be finite and positive.");
            if (!std::isfinite(GravityScale))
                throw std::invalid_argument("Rigid body gravity scale must be finite.");
            if (!std::isfinite(LinearDamping) || LinearDamping < 0.0f ||
                !std::isfinite(AngularDamping) || AngularDamping < 0.0f)
                throw std::invalid_argument("Rigid body damping must be finite and non-negative.");
        }
    };

    /// Persistent primitive collider descriptor in entity-local space.
    /// HalfExtents apply to boxes, Radius to spheres/capsules, and HalfHeight
    /// is the capsule segment half-height excluding its hemispherical caps.
    struct ColliderComponent final
    {
        ColliderShape Shape = ColliderShape::Box;
        kairo::foundation::math::Vec3f HalfExtents{ 0.5f, 0.5f, 0.5f };
        float Radius = 0.5f;
        float HalfHeight = 0.5f;
        float Friction = 0.5f;
        float Restitution = 0.1f;
        std::uint32_t BelongsTo = 1u;
        std::uint32_t CollidesWith = 0xFFFF'FFFFu;
        bool IsTrigger = false;

        void Validate() const
        {
            const auto positiveFinite = [](float value) {
                return std::isfinite(value) && value > 0.0f;
            };
            if (!positiveFinite(HalfExtents.x) || !positiveFinite(HalfExtents.y) ||
                !positiveFinite(HalfExtents.z) || !positiveFinite(Radius) ||
                !positiveFinite(HalfHeight))
                throw std::invalid_argument("Collider dimensions must be finite and positive.");
            if (!std::isfinite(Friction) || Friction < 0.0f ||
                !std::isfinite(Restitution) || Restitution < 0.0f || Restitution > 1.0f)
                throw std::invalid_argument(
                    "Collider friction must be non-negative and restitution must be in [0, 1].");
            if (BelongsTo == 0u)
                throw std::invalid_argument("Collider category mask must contain at least one bit.");
        }
    };
}
