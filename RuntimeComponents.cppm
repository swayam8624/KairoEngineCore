module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

        /// Task: validate the renderer-independent mesh/material references.
        /// Input: valid persistent KairoAssets handles of the required types.
        /// Output: no value; throws std::invalid_argument on an unusable component.
        /// This deliberately does not load assets: loading belongs to an adapter
        /// such as KairoRenderer, keeping EngineCore usable in headless tools.
        void Validate() const
        {
            if (!MeshAsset.IsValid() || !MaterialAsset.IsValid())
                throw std::invalid_argument("MeshRendererComponent requires valid mesh and material asset handles.");
        }
    };

    struct CameraComponent final
    {
        float VerticalFovRadians = 1.0471975512f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        bool Primary = false;

        /// Task: validate perspective parameters before a renderer derives a matrix.
        /// Input: vertical field of view in radians and positive near/far planes.
        /// Output: no value; throws std::invalid_argument for a non-projectable frustum.
        /// Convention: right-handed applications should use the same camera-space
        /// convention as their renderer; EngineCore stores no renderer-specific matrix.
        void Validate() const
        {
            if (!(VerticalFovRadians > 0.0f && VerticalFovRadians < 3.14159265f) || NearPlane <= 0.0f || FarPlane <= NearPlane)
                throw std::invalid_argument("CameraComponent has an invalid projection range.");
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
        }
    };
}
