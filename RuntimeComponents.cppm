module;
#include <cstdint>
#include <stdexcept>
#include <string>
export module Kairo.EngineCore.RuntimeComponents;
export namespace kairo::engine
{
    /// Opaque asset keys keep scene serialization independent from a concrete
    /// mesh/material loader while giving renderer adapters stable identifiers.
    struct MeshRendererComponent final
    {
        std::string MeshAsset;
        std::string MaterialAsset;
        bool Visible = true;

        /// Task: validate the renderer-independent mesh/material references.
        /// Input: non-empty, application-defined asset keys.
        /// Output: no value; throws std::invalid_argument on an unusable component.
        /// This deliberately does not load assets: loading belongs to an adapter
        /// such as KairoRenderer, keeping EngineCore usable in headless tools.
        void Validate() const
        {
            if (MeshAsset.empty() || MaterialAsset.empty()) throw std::invalid_argument("MeshRendererComponent requires mesh and material asset keys.");
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
    /// against a PhysicsWorld; zero means no attached runtime physics object.
    /// They intentionally carry no PhysicsEngine headers, preventing a Foundation
    /// dependency from leaking into the reusable engine core.
    struct RigidBodyComponent final { std::uint32_t Body = 0u; };
    struct ColliderComponent final { std::uint32_t Collider = 0u; };
}
