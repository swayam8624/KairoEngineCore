module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
export module Kairo.EngineCore.RuntimeComponents;

import Kairo.Assets;

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

    /// Non-owning body/collider references. Physics adapters resolve them
    /// against a PhysicsWorld. Component absence represents no attachment;
    /// numeric value zero remains available because KairoPhysicsEngine assigns
    /// valid body and collider IDs from zero. These types intentionally carry
    /// no PhysicsEngine headers, preventing that dependency from leaking into
    /// the reusable engine core.
    struct RigidBodyComponent final { std::uint32_t Body = 0u; };
    struct ColliderComponent final { std::uint32_t Collider = 0u; };
}
