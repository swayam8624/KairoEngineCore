// TEACHING MAP — Components.cppm
// Core component declarations and scene metadata. KairoRenderer consumes adapted render
// components, PhysicsEngine receives collider descriptors, while Scene owns persistent
// component state.
// See READING_ORDER.md and sibling Player/Renderer/Physics interfaces.

module;
#include <string>
export module Kairo.EngineCore.Components;
import Kairo.Foundation.Math;
export namespace kairo::engine
{
    /// Input: local position, orientation, and scale. Output: a KairoMath TRS.
    /// Task: canonical engine transform shared by renderer and physics bridges.
    struct TransformComponent final { kairo::foundation::math::Transform<float> Local{}; };
    struct NameComponent final { std::string Value = "Entity"; };
}
