#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>

import Kairo.EngineCore.AnimationController;
import Kairo.Assets;

using namespace kairo::assets;
using namespace kairo::engine;

namespace
{
    MeshArtifactData Triangle()
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

    GltfAnimationClipData TranslationClip(const char* name, float startX)
    {
        GltfAnimationClipData clip;
        clip.Name = name;
        GltfAnimationChannelData channel;
        channel.TargetNode = 0u;
        channel.Path = GltfAnimationPath::Translation;
        channel.Interpolation = GltfAnimationInterpolation::Linear;
        channel.Keyframes = {
            { 0.0f, { startX, 0.0f, 0.0f, 0.0f }, {}, {} },
            { 1.0f, { startX + 10.0f, 0.0f, 0.0f, 0.0f }, {}, {} }
        };
        clip.Channels.push_back(channel);
        return clip;
    }

    GltfSceneArtifactData ControllerScene()
    {
        GltfSceneArtifactData scene;
        GltfPrimitiveData primitive;
        primitive.Mesh = Triangle();
        scene.Primitives.push_back(std::move(primitive));

        GltfNodeData root;
        root.Name = "Root";
        root.HasRestTRS = true;
        root.PrimitiveIndices = { 0u };
        scene.Nodes.push_back(root);
        scene.RootNodes = { 0u };
        scene.Animations.push_back(TranslationClip("Idle", 0.0f));
        scene.Animations.push_back(TranslationClip("Walk", 100.0f));
        scene.Animations.push_back(TranslationClip("Run", 200.0f));
        ValidateGltfSceneArtifactData(scene);
        return scene;
    }

    AnimationControllerDefinition BasicDefinition()
    {
        AnimationControllerDefinition definition;
        definition.States = {
            { "Idle", 0u, 1.0f, AnimationTimeMode::Loop },
            { "Run", 2u, 1.0f, AnimationTimeMode::Loop }
        };
        AnimationControllerTransition transition;
        transition.FromState = 0u;
        transition.ToState = 1u;
        transition.BlendSeconds = 0.5f;
        transition.MinimumNormalizedTime = 0.25f;
        transition.Conditions.push_back({
            "speed", AnimationConditionOperator::GreaterEqual, 1.0
        });
        definition.Transitions.push_back(std::move(transition));
        return definition;
    }

    bool Near(float a, float b, float epsilon = 1.0e-4f)
    {
        return std::abs(a - b) <= epsilon;
    }
}

TEST_CASE("animation controller waits for exit progress then performs a deterministic crossfade")
{
    const auto scene = ControllerScene();
    AnimationControllerRuntime runtime(BasicDefinition(), scene);
    AnimationParameterSet parameters;
    parameters.SetFloat("speed", 2.0);

    auto pose = runtime.Advance(scene, parameters, 0.10f);
    CHECK_FALSE(runtime.IsTransitioning());
    CHECK(runtime.CurrentStateName() == "Idle");
    CHECK(Near(pose.Nodes[0].LocalTRS.Translation.x, 1.0f));

    pose = runtime.Advance(scene, parameters, 0.20f);
    CHECK(runtime.IsTransitioning());
    CHECK(runtime.CurrentStateName() == "Idle");
    CHECK(Near(runtime.TransitionAlpha(), 0.0f));
    CHECK(Near(pose.Nodes[0].LocalTRS.Translation.x, 3.0f));

    pose = runtime.Advance(scene, parameters, 0.25f);
    CHECK(runtime.IsTransitioning());
    CHECK(Near(runtime.TransitionAlpha(), 0.5f));
    // Source is Idle at t=.55 -> x=5.5; target is Run at t=.25 -> x=202.5.
    CHECK(Near(pose.Nodes[0].LocalTRS.Translation.x, 104.0f));

    pose = runtime.Advance(scene, parameters, 0.25f);
    CHECK_FALSE(runtime.IsTransitioning());
    CHECK(runtime.CurrentStateName() == "Run");
    CHECK(Near(runtime.CurrentTimeSeconds(), 0.5f));
    CHECK(Near(pose.Nodes[0].LocalTRS.Translation.x, 205.0f));
}

TEST_CASE("animation controller chooses highest priority passing transition")
{
    const auto scene = ControllerScene();
    AnimationControllerDefinition definition;
    definition.States = {
        { "Idle", 0u, 1.0f, AnimationTimeMode::Loop },
        { "Walk", 1u, 1.0f, AnimationTimeMode::Loop },
        { "Run", 2u, 1.0f, AnimationTimeMode::Loop }
    };

    AnimationControllerTransition walk;
    walk.FromState = 0u;
    walk.ToState = 1u;
    walk.BlendSeconds = 0.0f;
    walk.Priority = 1;
    walk.Conditions.push_back({ "speed", AnimationConditionOperator::Greater, 0.0 });
    definition.Transitions.push_back(walk);

    AnimationControllerTransition run = walk;
    run.ToState = 2u;
    run.Priority = 10;
    run.Conditions.front().Value = 1.0;
    definition.Transitions.push_back(run);

    AnimationParameterSet parameters;
    parameters.SetFloat("speed", 3.0);
    AnimationControllerRuntime runtime(definition, scene);
    const auto pose = runtime.Advance(scene, parameters, 0.0f);
    CHECK(runtime.CurrentStateName() == "Run");
    CHECK_FALSE(runtime.IsTransitioning());
    CHECK(Near(pose.Nodes[0].LocalTRS.Translation.x, 200.0f));
}

TEST_CASE("equal priority animation transitions are stable by authored order")
{
    const auto scene = ControllerScene();
    AnimationControllerDefinition definition;
    definition.States = {
        { "Idle", 0u, 1.0f, AnimationTimeMode::Loop },
        { "Walk", 1u, 1.0f, AnimationTimeMode::Loop },
        { "Run", 2u, 1.0f, AnimationTimeMode::Loop }
    };
    definition.Transitions.push_back({ 0u, 1u, {}, 0.0f, 0.0f, 5 });
    definition.Transitions.push_back({ 0u, 2u, {}, 0.0f, 0.0f, 5 });

    AnimationControllerRuntime runtime(definition, scene);
    AnimationParameterSet parameters;
    runtime.Advance(scene, parameters, 0.0f);
    CHECK(runtime.CurrentStateName() == "Walk");
}

TEST_CASE("animation conditions support typed booleans and numeric comparisons")
{
    const auto scene = ControllerScene();
    AnimationControllerDefinition definition;
    definition.States = {
        { "Idle", 0u, 1.0f, AnimationTimeMode::Clamp },
        { "Walk", 1u, 1.0f, AnimationTimeMode::Loop }
    };
    AnimationControllerTransition transition;
    transition.FromState = 0u;
    transition.ToState = 1u;
    transition.BlendSeconds = 0.0f;
    transition.Conditions = {
        { "grounded", AnimationConditionOperator::Equal, true },
        { "speed", AnimationConditionOperator::GreaterEqual, std::int64_t{ 2 } }
    };
    definition.Transitions.push_back(transition);

    AnimationControllerRuntime runtime(definition, scene);
    AnimationParameterSet parameters;
    parameters.SetBool("grounded", true);
    parameters.SetFloat("speed", 1.5);
    runtime.Advance(scene, parameters, 0.0f);
    CHECK(runtime.CurrentStateName() == "Idle");

    parameters.SetFloat("speed", 2.0);
    runtime.Advance(scene, parameters, 0.0f);
    CHECK(runtime.CurrentStateName() == "Walk");
}

TEST_CASE("animation controller validates state transition and runtime inputs")
{
    const auto scene = ControllerScene();

    auto duplicate = BasicDefinition();
    duplicate.States[1].Name = "Idle";
    CHECK_THROWS_AS(AnimationControllerRuntime(duplicate, scene), std::invalid_argument);

    auto badClip = BasicDefinition();
    badClip.States[1].ClipIndex = 999u;
    CHECK_THROWS_AS(AnimationControllerRuntime(badClip, scene), std::out_of_range);

    auto badBlend = BasicDefinition();
    badBlend.Transitions[0].BlendSeconds = -1.0f;
    CHECK_THROWS_AS(AnimationControllerRuntime(badBlend, scene), std::invalid_argument);

    AnimationControllerRuntime runtime(BasicDefinition(), scene);
    AnimationParameterSet parameters;
    CHECK_THROWS_AS(runtime.Advance(scene, parameters, -0.01f), std::invalid_argument);
    CHECK_THROWS_AS(parameters.SetFloat("bad", std::numeric_limits<double>::infinity()),
        std::invalid_argument);

    auto differentScene = scene;
    differentScene.Animations.pop_back();
    CHECK_THROWS_AS(runtime.EvaluatePose(differentScene), std::invalid_argument);
}
