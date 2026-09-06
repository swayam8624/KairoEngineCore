module;

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

export module Kairo.EngineCore.GltfSkinningRuntime;

import Kairo.Assets;
import Kairo.Foundation.Math;
import Kairo.EngineCore.AnimationRuntime;

export namespace kairo::engine
{
    /// Joint transforms expressed in imported-asset space.
    ///
    /// Each matrix is `jointWorld * inverseBindMatrix`, matching glTF skinning
    /// semantics before the outer scene/entity transform is applied. Keeping
    /// this result in asset space avoids requiring an inverse of the mesh node
    /// transform (which may legitimately contain a zero scale), and keeps the
    /// renderer independent from EngineCore scene/animation internals.
    struct GltfSkinPalette final
    {
        std::vector<kairo::foundation::math::Mat4f> JointMatrices;

        friend bool operator==(const GltfSkinPalette&, const GltfSkinPalette&) = default;
    };

    /// Input: validated imported glTF scene, one evaluated pose, and a skin.
    /// Output: one asset-space matrix per skin joint, in the exact palette order
    /// referenced by JOINTS_0 vertex indices.
    /// Task: create the CPU-side contract consumed by real-time GPU skinning.
    [[nodiscard]] inline GltfSkinPalette BuildGltfAssetSpaceSkinPalette(
        const kairo::assets::GltfSceneArtifactData& scene,
        const GltfAnimationPose& pose,
        std::size_t skinIndex)
    {
        kairo::assets::ValidateGltfSceneArtifactData(scene);
        if (skinIndex >= scene.Skins.size())
            throw std::out_of_range("glTF skin index is out of range.");

        const auto world = ResolveGltfWorldMatrices(scene, pose);
        const kairo::assets::GltfSkinData& skin = scene.Skins[skinIndex];

        GltfSkinPalette palette;
        palette.JointMatrices.reserve(skin.Joints.size());
        for (std::size_t index = 0u; index < skin.Joints.size(); ++index)
        {
            const std::uint32_t jointNode = skin.Joints[index];
            // ValidateGltfSceneArtifactData has already checked the joint index
            // and inverse-bind count; keep the multiplication here explicit so
            // the renderer never needs glTF hierarchy or bind-pose knowledge.
            palette.JointMatrices.push_back(
                world[jointNode] * GltfMatrixToKairo(skin.InverseBindMatrices[index]));
        }
        return palette;
    }

    /// Convenience overload for a mesh-bearing node that owns a skin binding.
    [[nodiscard]] inline GltfSkinPalette BuildGltfAssetSpaceSkinPaletteForNode(
        const kairo::assets::GltfSceneArtifactData& scene,
        const GltfAnimationPose& pose,
        std::size_t nodeIndex)
    {
        if (nodeIndex >= scene.Nodes.size())
            throw std::out_of_range("glTF skinned node index is out of range.");
        const std::uint32_t skinIndex = scene.Nodes[nodeIndex].SkinIndex;
        if (skinIndex == kairo::assets::GltfMissingIndex)
            throw std::invalid_argument("glTF node does not reference a skin.");
        return BuildGltfAssetSpaceSkinPalette(scene, pose, skinIndex);
    }
}