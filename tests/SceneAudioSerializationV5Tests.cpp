#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

import Kairo.Assets;
import Kairo.EngineCore.AudioRuntime;
import Kairo.EngineCore.AudioSceneComponents;
import Kairo.EngineCore.Scene;
import Kairo.EngineCore.SceneSerialization;
import Kairo.EngineCore.SceneSerializationV5;

namespace
{
    [[nodiscard]] kairo::assets::AssetHandle<kairo::assets::AssetType::Audio>
    RegisterAudio(kairo::assets::AssetRegistry& assets,
        std::string_view idText, std::string_view path)
    {
        kairo::assets::AssetMetadata metadata;
        metadata.ID = kairo::assets::AssetID::Parse(idText);
        metadata.Type = kairo::assets::AssetType::Audio;
        metadata.Origin = kairo::assets::AssetOrigin::SourceFile;
        metadata.Path = std::string(path);
        metadata.Importer = "kairo.audio.wav";
        assets.Insert(metadata);
        return { metadata.ID };
    }

    [[nodiscard]] kairo::assets::TextureAssetHandle RegisterTexture(
        kairo::assets::AssetRegistry& assets,
        std::string_view idText, std::string_view path)
    {
        kairo::assets::AssetMetadata metadata;
        metadata.ID = kairo::assets::AssetID::Parse(idText);
        metadata.Type = kairo::assets::AssetType::Texture2D;
        metadata.Origin = kairo::assets::AssetOrigin::SourceFile;
        metadata.Path = std::string(path);
        metadata.Importer = "kairo.image";
        assets.Insert(metadata);
        return { metadata.ID };
    }
}

TEST_CASE("Kairo scene v5 round-trips complete authored audio deterministically")
{
    using namespace kairo::engine;
    kairo::assets::AssetRegistry assets;
    const auto clip = RegisterAudio(assets,
        "12345678-1234-4234-9234-123456789abc", "Audio/engine.wav");

    Scene source;
    const auto emitterEntity = source.CreateEntityWithID({ 4u }, "Engine");
    const auto listenerEntity = source.CreateEntityWithID({ 9u }, "Camera");

    AudioEmitterComponent emitter;
    emitter.Clip = clip;
    emitter.Enabled = false;
    emitter.PlayOnStart = false;
    emitter.Loop = true;
    emitter.Spatial = true;
    emitter.Gain = 0.625;
    emitter.Pitch = 1.125;
    emitter.Attenuation.Model = AudioDistanceModel::Linear;
    emitter.Attenuation.MinDistance = 2.5;
    emitter.Attenuation.MaxDistance = 72.0;
    emitter.Attenuation.Rolloff = 0.65;
    emitter.Bus = "vehicles main\tbus";
    emitter.Priority = -17;
    source.SetAudioEmitter(emitterEntity, emitter);

    AudioListenerComponent listener;
    listener.Enabled = true;
    listener.Primary = true;
    source.SetAudioListener(listenerEntity, listener);

    const std::string first = SerializeSceneV5(source, assets);
    const std::string second = SerializeSceneV5(source, assets);
    REQUIRE(first == second);
    REQUIRE(first.starts_with("kairo-scene 5\n"));
    REQUIRE(first.find("audio-emitter ") != std::string::npos);
    REQUIRE(first.find("audio-listener true true") != std::string::npos);

    const Scene restored = ParseSceneV5(first, assets);
    REQUIRE(restored.Entities() == source.Entities());
    REQUIRE(restored.HasAudioEmitter(emitterEntity));
    const auto& restoredEmitter = restored.AudioEmitter(emitterEntity);
    CHECK(restoredEmitter.Clip == clip);
    CHECK_FALSE(restoredEmitter.Enabled);
    CHECK_FALSE(restoredEmitter.PlayOnStart);
    CHECK(restoredEmitter.Loop);
    CHECK(restoredEmitter.Spatial);
    CHECK(restoredEmitter.Gain == emitter.Gain);
    CHECK(restoredEmitter.Pitch == emitter.Pitch);
    CHECK(restoredEmitter.Attenuation.Model == AudioDistanceModel::Linear);
    CHECK(restoredEmitter.Attenuation.MinDistance == emitter.Attenuation.MinDistance);
    CHECK(restoredEmitter.Attenuation.MaxDistance == emitter.Attenuation.MaxDistance);
    CHECK(restoredEmitter.Attenuation.Rolloff == emitter.Attenuation.Rolloff);
    CHECK(restoredEmitter.Bus == emitter.Bus);
    CHECK(restoredEmitter.Priority == emitter.Priority);

    REQUIRE(restored.HasAudioListener(listenerEntity));
    const auto& restoredListener = restored.AudioListenerComponentFor(listenerEntity);
    CHECK(restoredListener.Enabled);
    CHECK(restoredListener.Primary);
    CHECK(SerializeSceneV5(restored, assets) == first);
}

TEST_CASE("Kairo scene v5 loader delegates legacy v4 scenes unchanged")
{
    using namespace kairo::engine;
    kairo::assets::AssetRegistry assets;
    constexpr std::string_view legacy =
        "kairo-scene 4\n"
        "entity 1 \"Legacy\"\n"
        "enabled true\n"
        "layer 0\n"
        "transform 1 2 3 0 0 0 1 1 1 1\n"
        "end\n";

    const Scene scene = ParseSceneV5(legacy, assets);
    REQUIRE(scene.Contains({ 1u }));
    CHECK(scene.Name({ 1u }).Value == "Legacy");
    CHECK(scene.WorldTransform({ 1u }).Translation.x == 1.0f);
    CHECK_FALSE(scene.HasAudioEmitter({ 1u }));
    CHECK_FALSE(scene.HasAudioListener({ 1u }));
}

TEST_CASE("Kairo scene v5 rejects non-audio assets for emitter clips")
{
    using namespace kairo::engine;
    kairo::assets::AssetRegistry assets;
    const auto texture = RegisterTexture(assets,
        "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", "Textures/not-audio.png");

    const std::string source =
        "kairo-scene 5\n"
        "entity 1 \"Emitter\"\n"
        "enabled true\n"
        "layer 0\n"
        "transform 0 0 0 0 0 0 1 1 1 1\n"
        "audio-emitter " + texture.ID.ToString() +
        " true true false true 1 1 inverse 1 100 1 \"master\" 0\n"
        "end\n";

    CHECK_THROWS_AS(ParseSceneV5(source, assets), SceneFormatError);
}

TEST_CASE("Kairo scene v5 rejects duplicate audio components before mutation")
{
    using namespace kairo::engine;
    kairo::assets::AssetRegistry assets;
    const auto clip = RegisterAudio(assets,
        "bbbbbbbb-cccc-4ddd-8eee-ffffffffffff", "Audio/voice.wav");
    const std::string emitter =
        "audio-emitter " + clip.ID.ToString() +
        " true true false true 1 1 inverse 1 100 1 \"master\" 0\n";
    const std::string source =
        "kairo-scene 5\n"
        "entity 1 \"Emitter\"\n"
        "enabled true\n"
        "layer 0\n"
        "transform 0 0 0 0 0 0 1 1 1 1\n" +
        emitter + emitter +
        "end\n";

    CHECK_THROWS_AS(ParseSceneV5(source, assets), SceneFormatError);
}
