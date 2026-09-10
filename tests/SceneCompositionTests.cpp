#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

import Kairo.Assets;
import Kairo.EngineCore.SceneComposition;
import Kairo.EngineCore.Scene;
import Kairo.EngineCore.RuntimeComponents;
import Kairo.EngineCore.AudioSceneComponents;

using namespace kairo::engine;

namespace
{
    Scene FragmentScene()
    {
        Scene scene;
        const Entity root = scene.CreateEntityWithID({ 100u }, "CellRoot");
        const Entity child = scene.CreateEntityWithID({ 250u }, "CellChild");
        scene.Transform(root).Local.Translation = { 10.0f, 0.0f, 20.0f };
        scene.Transform(child).Local.Translation = { 0.0f, 2.0f, 0.0f };
        scene.SetLayer(root, 4u);
        scene.AddTag(root, "streamed");
        scene.SetEnabled(child, false);
        scene.SetParent(child, root);

        CameraComponent camera;
        camera.Primary = false;
        scene.SetCamera(root, camera);

        LightComponent light;
        scene.SetLight(child, light);

        AudioEmitterComponent emitter;
        emitter.Clip = {
            kairo::assets::AssetID::Parse(
                "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee") };
        emitter.Loop = true;
        emitter.Gain = 0.42;
        emitter.Bus = "world";
        scene.SetAudioEmitter(root, emitter);

        AudioListenerComponent listener;
        listener.Enabled = true;
        listener.Primary = false;
        scene.SetAudioListener(child, listener);
        return scene;
    }
}

TEST_CASE("scene composition remaps IDs while preserving authored state and hierarchy")
{
    Scene destination;
    const Entity persistent = destination.CreateEntity("Persistent");
    destination.AddTag(persistent, "persistent");

    const Scene source = FragmentScene();
    const SceneAppendResult appended = AppendScene(destination, source);
    REQUIRE(appended.Size() == 2u);
    CHECK(destination.Size() == 3u);

    const auto root = appended.Resolve({ 100u });
    const auto child = appended.Resolve({ 250u });
    REQUIRE(root.has_value());
    REQUIRE(child.has_value());
    CHECK(*root != Entity{ 100u });
    CHECK(*child != Entity{ 250u });
    CHECK(destination.Name(*root).Value == "CellRoot");
    CHECK(destination.Name(*child).Value == "CellChild");
    CHECK(destination.Layer(*root) == 4u);
    CHECK(destination.HasTag(*root, "streamed"));
    CHECK_FALSE(destination.IsEnabled(*child));
    REQUIRE(destination.Parent(*child).has_value());
    CHECK(destination.Parent(*child) == root);
    CHECK(destination.Transform(*root).Local.Translation.x == 10.0f);
    CHECK(destination.Transform(*root).Local.Translation.z == 20.0f);
    CHECK(destination.HasCamera(*root));
    CHECK(destination.HasLight(*child));
    REQUIRE(destination.HasAudioEmitter(*root));
    CHECK(destination.AudioEmitter(*root).Clip.ID ==
        kairo::assets::AssetID::Parse(
            "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"));
    CHECK(destination.AudioEmitter(*root).Loop);
    CHECK(destination.AudioEmitter(*root).Gain == 0.42);
    CHECK(destination.AudioEmitter(*root).Bus == "world");
    REQUIRE(destination.HasAudioListener(*child));
    CHECK(destination.AudioListenerComponentFor(*child).Enabled);
    CHECK_FALSE(destination.AudioListenerComponentFor(*child).Primary);
    CHECK(destination.Contains(persistent));
}

TEST_CASE("scene append is transactional when authored components conflict")
{
    Scene destination;
    const Entity persistentCamera = destination.CreateEntity("PersistentCamera");
    CameraComponent existing;
    existing.Primary = true;
    destination.SetCamera(persistentCamera, existing);

    Scene source;
    const Entity streamedCamera = source.CreateEntity("StreamedCamera");
    CameraComponent incoming;
    incoming.Primary = true;
    source.SetCamera(streamedCamera, incoming);

    REQUIRE(destination.Size() == 1u);
    CHECK_THROWS_AS(AppendScene(destination, source), std::invalid_argument);
    CHECK(destination.Size() == 1u);
    CHECK(destination.Contains(persistentCamera));
    CHECK(destination.Camera(persistentCamera).Primary);
    CHECK(destination.Name(persistentCamera).Value == "PersistentCamera");
}

TEST_CASE("scene append keeps primary-listener conflicts transactional")
{
    Scene destination;
    const Entity persistentListener = destination.CreateEntity("PersistentListener");
    AudioListenerComponent existing;
    existing.Primary = true;
    destination.SetAudioListener(persistentListener, existing);

    Scene source;
    const Entity streamedListener = source.CreateEntity("StreamedListener");
    AudioListenerComponent incoming;
    incoming.Primary = true;
    source.SetAudioListener(streamedListener, incoming);

    REQUIRE(destination.Size() == 1u);
    CHECK_THROWS_AS(AppendScene(destination, source), std::invalid_argument);
    CHECK(destination.Size() == 1u);
    CHECK(destination.Contains(persistentListener));
    CHECK(destination.AudioListenerComponentFor(persistentListener).Primary);
    CHECK(destination.Name(persistentListener).Value == "PersistentListener");
}

TEST_CASE("scene composition removal preserves persistent entities and external parents")
{
    Scene destination;
    const Entity persistent = destination.CreateEntity("WorldRoot");
    const auto appended = AppendScene(destination, FragmentScene());
    const Entity streamedRoot = *appended.Resolve({ 100u });
    destination.SetParent(streamedRoot, persistent);

    const std::size_t removed = RemoveAppendedScene(destination, appended);
    CHECK(removed == 2u);
    CHECK(destination.Size() == 1u);
    CHECK(destination.Contains(persistent));
    CHECK(destination.Name(persistent).Value == "WorldRoot");
    CHECK(destination.Children(persistent).empty());
}

TEST_CASE("scene composition refuses unload when a persistent child depends on streamed ownership")
{
    Scene destination;
    const auto appended = AppendScene(destination, FragmentScene());
    const Entity streamedRoot = *appended.Resolve({ 100u });
    const Entity external = destination.CreateEntity("GameplaySpawn");
    destination.SetParent(external, streamedRoot);

    const std::size_t sizeBefore = destination.Size();
    CHECK_THROWS_AS(RemoveAppendedScene(destination, appended), std::logic_error);
    CHECK(destination.Size() == sizeBefore);
    CHECK(destination.Contains(streamedRoot));
    CHECK(destination.Contains(external));
    REQUIRE(destination.Parent(external).has_value());
    CHECK(*destination.Parent(external) == streamedRoot);
}

TEST_CASE("scene composition tolerates fragments already removed by gameplay")
{
    Scene destination;
    const auto appended = AppendScene(destination, FragmentScene());
    const Entity streamedRoot = *appended.Resolve({ 100u });
    destination.DestroyEntity(streamedRoot);

    CHECK(RemoveAppendedScene(destination, appended) == 0u);
    CHECK(destination.Size() == 0u);
}
