module;
#include <cstdint>
#include <stdexcept>
export module Kairo.EngineCore.RuntimeComponents;

import Kairo.Assets;

export namespace kairo::engine
{
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
