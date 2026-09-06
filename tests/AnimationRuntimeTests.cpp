#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

import Kairo.EngineCore;

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

    [[nodiscard]] GltfSceneArtifactData RuntimeScene()
    {
        GltfSceneArtifactData scene;
        GltfPrimitiveData primitive;
        primitive.Mesh = Triangle();
        scene.Primitives.push_back(primitive);

        GltfNodeData root;
        root.Name = "Root";
        root.HasRestTRS = true;
        root.RestTranslation = { 10.0f, 0.0f, 0.0f };
        scene.Nodes.push_back(root);

        GltfNodeData child;
        child.Name = "AnimatedChild";
        child.Parent = 0;
        child.HasRestTRS = true;
        child.RestTranslation = { 0.0f, 2.0f, 0.0f };
        child.RestScale = { 2.0f, 2.0f, 2.0f };
        child.PrimitiveIndices = { 0u };
        scene.Nodes.push_back(child);

        GltfNodeData matrixNode;
        matrixNode.Name = "StaticMatrixChild";
        matrixNode.Parent = 0;
        matrixNode.LocalTransform = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 3.0f, 1.0f };
        scene.Nodes.push_back(matrixNode);
        scene.RootNodes = { 0u };

        GltfAnimationClipData motion;
        motion.Name = "Motion";

        GltfAnimationChannelData translation;
        translation.TargetNode = 1u;
        translation.Path = GltfAnimationPath::Translation;
        translation.Interpolation = GltfAnimationInterpolation::Linear;
        translation.Keyframes = {
            { 0.0f, { 0.0f, 2.0f, 0.0f, 0.0f }, {}, {} },
            { 2.0f, { 4.0f, 2.0f, 0.0f, 0.0f }, {}, {} }
        };
        motion.Channels.push_back(translation);

        GltfAnimationChannelData rotation;
        rotation.TargetNode = 0u;
        rotation.Path = GltfAnimationPath::Rotation;
        rotation.Interpolation = GltfAnimationInterpolation::Linear;
        rotation.Keyframes = {
            { 0.0f, { 0.0f, 0.0f, 0.0f, 1.0f }, {}, {} },
            { 2.0f, { 0.0f, 1.0f, 0.0f, 0.0f }, {}, {} }
        };
        motion.Channels.push_back(rotation);

        GltfAnimationChannelData scale;
        scale.TargetNode = 1u;
        scale.Path = GltfAnimationPath::Scale;
        scale.Interpolation = GltfAnimationInterpolation::Step;
        scale.Keyframes = {
            { 0.0f, { 2.0f, 2.0f, 2.0f, 0.0f }, {}, {} },
            { 1.0f, { 3.0f, 3.0f, 3.0f, 0.0f }, {}, {} },
            { 2.0f, { 4.0f, 4.0f, 4.0f, 0.0f }, {}, {} }
        };
        motion.Channels.push_back(scale);
        scene.Animations.push_back(motion);

        GltfAnimationClipData partial;
        partial.Name = "Partial";
        partial.Channels.push_back(translation);
        scene.Animations.push_back(partial);

        GltfAnimationClipData cubic;
        cubic.Name = "Cubic";
        GltfAnimationChannelData curve;
        curve.TargetNode = 1u;
        curve.Path = GltfAnimationPath::Translation;
        curve.Interpolation = GltfAnimationInterpolation::CubicSpline;
        curve.Keyframes = {
            { 0.0f, { 0.0f, 2.0f, 0.0f, 0.0f }, {}, { 2.0f, 0.0f, 0.0f, 0.0f } },
            { 1.0f, { 1.0f, 2.0f, 0.0f, 0.0f }, {}, {} }
        };
        cubic.Channels.push_back(curve);
        scene.Animations.push_back(cubic);

        ValidateGltfSceneArtifactData(scene);
        return scene;
    }

    [[nodiscard]] bool Near(float a, float b, float epsilon = 1.0e-4f)
    {
        return std::abs(a - b) <= epsilon;
    }
}

TEST_CASE("glTF rest pose preserves TRS and matrix authored nodes")
{
    const auto scene = RuntimeScene();
    const auto pose = BuildGltfRestPose(scene);
    REQUIRE(pose.Nodes.size() == 3u);
    CHECK(pose.Nodes[0].UsesTRS);
    CHECK(pose.Nodes[1].UsesTRS);
    CHECK_FALSE(pose.Nodes[2].UsesTRS);
    CHECK(pose.Nodes[1].LocalTRS.Translation.y == 2.0f);
    CHECK(pose.Nodes[1].LocalTRS.Scale.x == 2.0f);
    CHECK(pose.Nodes[2].LocalMatrix(2u, 3u) == 3.0f);

    const auto world = ResolveGltfWorldMatrices(scene, pose);
    REQUIRE(world.size() == 3u);
    CHECK(Near(world[0](0u, 3u), 10.0f));
    CHECK(Near(world[1](0u, 3u), 10.0f));
    CHECK(Near(world[1](1u, 3u), 2.0f));
    CHECK(Near(world[2](0u, 3u), 10.0f));
    CHECK(Near(world[2](2u, 3u), 3.0f));
}

TEST_CASE("glTF animation samples linear rotation and step channels")
{
    const auto scene = RuntimeScene();
    const auto pose = SampleGltfAnimation(scene, 0u, 1.5f);
    CHECK(Near(pose.Nodes[1].LocalTRS.Translation.x, 3.0f));
    CHECK(Near(pose.Nodes[1].LocalTRS.Translation.y, 2.0f));
    CHECK(Near(pose.Nodes[1].LocalTRS.Scale.x, 3.0f));

    const auto half = SampleGltfAnimation(scene, 0u, 1.0f);
    const float rootHalf = std::sqrt(0.5f);
    CHECK(Near(std::abs(half.Nodes[0].LocalTRS.Rotation.y), rootHalf));
    CHECK(Near(std::abs(half.Nodes[0].LocalTRS.Rotation.w), rootHalf));
}

TEST_CASE("partial animation channels preserve untouched rest components")
{
    const auto scene = RuntimeScene();
    const auto pose = SampleGltfAnimation(scene, 1u, 1.0f);
    CHECK(Near(pose.Nodes[1].LocalTRS.Translation.x, 2.0f));
    CHECK(pose.Nodes[1].LocalTRS.Scale.x == 2.0f);
    CHECK(pose.Nodes[1].LocalTRS.Scale.y == 2.0f);
    CHECK(pose.Nodes[1].LocalTRS.Rotation.w == 1.0f);
}

TEST_CASE("glTF animation cubic spline uses time-scaled Hermite tangents")
{
    const auto scene = RuntimeScene();
    const auto pose = SampleGltfAnimation(scene, 2u, 0.5f);
    CHECK(Near(pose.Nodes[1].LocalTRS.Translation.x, 0.75f));
    CHECK(Near(pose.Nodes[1].LocalTRS.Translation.y, 2.0f));
}

TEST_CASE("animation clamp loop and pose blending are deterministic")
{
    const auto scene = RuntimeScene();
    const auto clamped = SampleGltfAnimation(scene, 0u, 100.0f, AnimationTimeMode::Clamp);
    CHECK(Near(clamped.Nodes[1].LocalTRS.Translation.x, 4.0f));
    CHECK(Near(clamped.Nodes[1].LocalTRS.Scale.x, 4.0f));

    const auto looped = SampleGltfAnimation(scene, 0u, 2.5f, AnimationTimeMode::Loop);
    CHECK(Near(looped.Nodes[1].LocalTRS.Translation.x, 1.0f));
    CHECK(Near(looped.Nodes[1].LocalTRS.Scale.x, 2.0f));

    const auto start = SampleGltfAnimation(scene, 0u, 0.0f);
    const auto end = SampleGltfAnimation(scene, 0u, 2.0f);
    const auto blended = BlendGltfAnimationPoses(start, end, 0.5f);
    CHECK(Near(blended.Nodes[1].LocalTRS.Translation.x, 2.0f));
    CHECK(Near(blended.Nodes[1].LocalTRS.Scale.x, 3.0f));
    const float half = std::sqrt(0.5f);
    CHECK(Near(std::abs(blended.Nodes[0].LocalTRS.Rotation.y), half));
    CHECK(Near(std::abs(blended.Nodes[0].LocalTRS.Rotation.w), half));
}

TEST_CASE("world matrix resolution supports parents appearing after children")
{
    auto scene = RuntimeScene();
    std::swap(scene.Nodes[0], scene.Nodes[1]);
    scene.Nodes[0].Parent = 1;
    scene.Nodes[1].Parent = -1;
    scene.RootNodes = { 1u };
    for (auto& clip : scene.Animations)
        for (auto& channel : clip.Channels)
            channel.TargetNode = channel.TargetNode == 0u ? 1u : 0u;
    ValidateGltfSceneArtifactData(scene);

    const auto pose = BuildGltfRestPose(scene);
    const auto world = ResolveGltfWorldMatrices(scene, pose);
    CHECK(Near(world[0](0u, 3u), 10.0f));
    CHECK(Near(world[0](1u, 3u), 2.0f));
}
