#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

import Kairo.Assets;
import Kairo.EngineCore.AudioRuntime;
import Kairo.EngineCore.AudioSceneComponents;
import Kairo.EngineCore.AudioScenePersistence;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.SaveGame;
import Kairo.EngineCore.Scene;

namespace
{
    using AudioAssetHandle =
        kairo::assets::AssetHandle<kairo::assets::AssetType::Audio>;

    [[nodiscard]] AudioAssetHandle RegisterAudio(
        kairo::assets::AssetRegistry& assets, std::string_view idText,
        std::string_view path)
    {
        kairo::assets::AssetMetadata metadata;
        metadata.ID = kairo::assets::AssetID::Parse(idText);
        metadata.Type = kairo::assets::AssetType::Audio;
        metadata.Origin = kairo::assets::AssetOrigin::SourceFile;
        metadata.Path = std::string(path);
        metadata.Importer = "kairo.wav";
        assets.Insert(metadata);
        return { metadata.ID };
    }
}

TEST_CASE("Audio scene state round-trips deterministically")
{
    using namespace kairo::engine;
    kairo::assets::AssetRegistry assets;
    const auto clip = RegisterAudio(assets,
        "12345678-1234-4234-9234-123456789abc", "Audio/engine.wav");

    Scene source;
    const Entity emitterEntity = source.CreateEntityWithID({ 4u }, "Engine");
    const Entity listenerEntity = source.CreateEntityWithID({ 9u }, "Camera");

    AudioEmitterComponent emitter;
    emitter.Clip = clip;
    emitter.Loop = true;
    emitter.Spatial = true;
    emitter.Gain = 0.65;
    emitter.Pitch = 1.15;
    emitter.Attenuation.Model = AudioDistanceModel::Linear;
    emitter.Attenuation.MinDistance = 2.0;
    emitter.Attenuation.MaxDistance = 48.0;
    emitter.Attenuation.Rolloff = 0.7;
    emitter.Bus = "vehicles";
    emitter.Priority = 17;
    source.SetAudioEmitter(emitterEntity, emitter);

    AudioListenerComponent listener;
    listener.Primary = true;
    source.SetAudioListener(listenerEntity, listener);

    const auto first = SerializeAudioSceneState(source);
    const auto second = SerializeAudioSceneState(source);
    REQUIRE(first == second);

    Scene destination;
    (void)destination.CreateEntityWithID({ 4u }, "Engine");
    (void)destination.CreateEntityWithID({ 9u }, "Camera");
    ApplyAudioSceneState(first, destination, assets);

    REQUIRE(destination.HasAudioEmitter({ 4u }));
    const auto& restored = destination.AudioEmitter({ 4u });
    CHECK(restored.Clip == clip);
    CHECK(restored.Loop);
    CHECK(restored.Gain == 0.65);
    CHECK(restored.Pitch == 1.15);
    CHECK(restored.Bus == "vehicles");
    CHECK(restored.Priority == 17);
    REQUIRE(destination.ActiveAudioListener() == Entity{ 9u });
    CHECK(SerializeAudioSceneState(destination) == first);

    const SaveGameChunk chunk = MakeAudioSceneSaveChunk(source);
    CHECK(chunk.Name == AudioSceneSaveChunkName);
    CHECK(chunk.SchemaVersion == AudioSceneSaveChunkSchema);
}

TEST_CASE("Audio scene restore validates before mutation")
{
    using namespace kairo::engine;
    kairo::assets::AssetRegistry sourceAssets;
    const auto clip = RegisterAudio(sourceAssets,
        "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", "Audio/source.wav");

    Scene source;
    const Entity entity = source.CreateEntityWithID({ 3u }, "Emitter");
    AudioEmitterComponent emitter;
    emitter.Clip = clip;
    source.SetAudioEmitter(entity, emitter);
    const auto payload = SerializeAudioSceneState(source);

    kairo::assets::AssetRegistry wrongAssets;
    kairo::assets::AssetMetadata wrong;
    wrong.ID = clip.ID;
    wrong.Type = kairo::assets::AssetType::Texture2D;
    wrong.Origin = kairo::assets::AssetOrigin::SourceFile;
    wrong.Path = "Textures/not-audio.png";
    wrong.Importer = "kairo.image";
    wrongAssets.Insert(wrong);

    Scene destination;
    (void)destination.CreateEntityWithID({ 3u }, "Emitter");
    const auto sentinelClip = RegisterAudio(wrongAssets,
        "11111111-2222-4333-8444-555555555555", "Audio/sentinel.wav");
    AudioEmitterComponent sentinel;
    sentinel.Clip = sentinelClip;
    sentinel.Gain = 0.25;
    destination.SetAudioEmitter({ 3u }, sentinel);

    CHECK_THROWS(ApplyAudioSceneState(payload, destination, wrongAssets));
    REQUIRE(destination.HasAudioEmitter({ 3u }));
    CHECK(destination.AudioEmitter({ 3u }).Clip == sentinelClip);
    CHECK(destination.AudioEmitter({ 3u }).Gain == 0.25);
}
