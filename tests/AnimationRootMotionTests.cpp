#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>

import Kairo.EngineCore;
import Kairo.Foundation.Math;

using namespace kairo::assets;
using namespace kairo::engine;

namespace
{
    [[nodiscard]] MeshArtifactData Triangle()
    {
        MeshArtifactData mesh;
        mesh.Vertices = {
            { { 0.0f, 0.0f, 0.0f }, {}, {} },
            { { 1.0f, 0.0f, 0.0f }, {}, {} },
            { { 0.0f, 1.0f, 0.0f }, {}, {} }
        };
        mesh.Indices = { 0u, 1u, 2u };
        return mesh;
    }

    [[nodiscard]] GltfSceneArtifactData RootMotionScene()
    {
        GltfSceneArtifactData scene;
        GltfPrimitiveData primitive;
        primitive.Mesh = Triangle();
        scene.Primitives.push_back(primitive);

        GltfNodeData root;
        root.Name = "MotionRoot";
        root.HasRestTRS = true;
        root.RestTranslation = { 0.0f, 1.0f, 0.0f };
        root.PrimitiveIndices = { 0u };
        scene.Nodes.push_back(root);
        scene.RootNodes = { 0u };

        GltfAnimationClipData locomotion;
        locomotion.Name = "Locomotion";

        GltfAnimationChannelData translation;
        translation.TargetNode = 0u;
        translation.Path = GltfAnimationPath::Translation;
        translation.Interpolation = GltfAnimationInterpolation::Linear;
        translation.Keyframes = {
            { 0.0f, { 0.0f, 1.0f, 0.0f, 0.0f }, {}, {} },
            { 2.0f, { 4.0f, 3.0f, 0.0f, 0.0f }, {}, {} }
        };
        locomotion.Channels.push_back(translation);

        GltfAnimationChannelData rotation;
        rotation.TargetNode = 0u;
        rotation.Path = GltfAnimationPath::Rotation;
        rotation.Interpolation = GltfAnimationInterpolation::Linear;
        rotation.Keyframes = {
            { 0.0f, { 0.0f, 0.0f, 0.0f, 1.0f }, {}, {} },
            { 2.0f, { 0.0f, 1.0f, 0.0f, 0.0f }, {}, {} }
        };
        locomotion.Channels.push_back(rotation);
        scene.Animations.push_back(locomotion);

        ValidateGltfSceneArtifactData(scene);
        return scene;
    }

    [[nodiscard]] bool Near(float a, float b, float epsilon = 1.0e-4f)
    {
        return std::abs(a - b) <= epsilon;
    }
}

TEST_CASE("root motion extracts translation and relative rotation in clamp mode")
{
    const auto scene = RootMotionScene();
    const auto delta = ExtractGltfRootMotion(
        scene, 0u, 0u, 0.5f, 1.5f, AnimationTimeMode::Clamp);

    CHECK(Near(delta.Translation.x, 2.0f));
    CHECK(Near(delta.Translation.y, 1.0f));
    CHECK(Near(delta.Translation.z, 0.0f));
    const float half = std::sqrt(0.5f);
    CHECK(Near(std::abs(delta.Rotation.y), half));
    CHECK(Near(std::abs(delta.Rotation.w), half));
}

TEST_CASE("root motion accumulates cleanly across a loop boundary")
{
    const auto scene = RootMotionScene();
    const auto delta = ExtractGltfRootMotion(
        scene, 0u, 0u, 1.5f, 2.5f, AnimationTimeMode::Loop);

    CHECK(Near(delta.Translation.x, 2.0f));
    CHECK(Near(delta.Translation.y, 1.0f));
    const float half = std::sqrt(0.5f);
    CHECK(Near(std::abs(delta.Rotation.y), half));
    CHECK(Near(std::abs(delta.Rotation.w), half));
}

TEST_CASE("root motion accumulates multiple complete loops without a backwards jump")
{
    const auto scene = RootMotionScene();
    const auto delta = ExtractGltfRootMotion(
        scene, 0u, 0u, 0.5f, 4.5f, AnimationTimeMode::Loop);

    CHECK(Near(delta.Translation.x, 8.0f));
    CHECK(Near(delta.Translation.y, 4.0f));
    CHECK(kairo::foundation::math::NearlyEqual(
        delta.Rotation,
        kairo::foundation::math::Quatf::Identity(),
        1.0e-4f));
}

TEST_CASE("root motion handles very large skipped loop counts without per-loop work")
{
    const auto scene = RootMotionScene();
    // Same local sample after 100,000 complete 2-second cycles. This protects
    // the hitch/server-resume path from accidentally regressing to O(loopCount).
    const auto delta = ExtractGltfRootMotion(
        scene, 0u, 0u, 0.5f, 200000.5f, AnimationTimeMode::Loop);

    CHECK(Near(delta.Translation.x, 400000.0f, 0.05f));
    CHECK(Near(delta.Translation.y, 200000.0f, 0.05f));
    CHECK(kairo::foundation::math::NearlyEqual(
        delta.Rotation,
        kairo::foundation::math::Quatf::Identity(),
        1.0e-4f));
}

TEST_CASE("root motion treats an exact loop endpoint as the clip end")
{
    const auto scene = RootMotionScene();
    const auto delta = ExtractGltfRootMotion(
        scene, 0u, 0u, 1.5f, 2.0f, AnimationTimeMode::Loop);

    CHECK(Near(delta.Translation.x, 1.0f));
    CHECK(Near(delta.Translation.y, 0.5f));
    const float sin22_5 = std::sin(3.14159265358979323846f / 8.0f);
    const float cos22_5 = std::cos(3.14159265358979323846f / 8.0f);
    CHECK(Near(std::abs(delta.Rotation.y), sin22_5));
    CHECK(Near(std::abs(delta.Rotation.w), cos22_5));
}

TEST_CASE("root motion channel masks and pose removal prevent double motion")
{
    const auto scene = RootMotionScene();

    GltfRootMotionSettings extraction;
    extraction.TranslationX = true;
    extraction.TranslationY = false;
    extraction.TranslationZ = false;
    extraction.Rotation = false;
    const auto masked = ExtractGltfRootMotion(
        scene, 0u, 0u, 0.0f, 1.0f,
        AnimationTimeMode::Clamp, extraction);
    CHECK(Near(masked.Translation.x, 2.0f));
    CHECK(Near(masked.Translation.y, 0.0f));
    CHECK(kairo::foundation::math::NearlyEqual(
        masked.Rotation,
        kairo::foundation::math::Quatf::Identity(),
        1.0e-5f));

    auto pose = SampleGltfAnimation(scene, 0u, 1.0f, AnimationTimeMode::Clamp);
    REQUIRE(Near(pose.Nodes[0].LocalTRS.Translation.x, 2.0f));
    REQUIRE(Near(pose.Nodes[0].LocalTRS.Translation.y, 2.0f));

    GltfRootMotionSettings removal;
    removal.TranslationX = true;
    removal.TranslationY = false;
    removal.TranslationZ = false;
    removal.Rotation = true;
    RemoveGltfRootMotionFromPose(scene, 0u, pose, removal);

    CHECK(Near(pose.Nodes[0].LocalTRS.Translation.x, 0.0f));
    CHECK(Near(pose.Nodes[0].LocalTRS.Translation.y, 2.0f));
    CHECK(kairo::foundation::math::NearlyEqual(
        pose.Nodes[0].LocalTRS.Rotation,
        kairo::foundation::math::Quatf::Identity(),
        1.0e-5f));
}

TEST_CASE("root motion rejects non-monotonic time and non-TRS motion nodes")
{
    auto scene = RootMotionScene();
    CHECK_THROWS_AS(
        ExtractGltfRootMotion(scene, 0u, 0u, 1.0f, 0.5f),
        std::invalid_argument);

    scene.Nodes[0].HasRestTRS = false;
    CHECK_THROWS_AS(
        ExtractGltfRootMotion(scene, 0u, 0u, 0.0f, 0.5f),
        std::invalid_argument);
}
