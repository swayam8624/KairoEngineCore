#include <catch2/catch_test_macros.hpp>

import Kairo.Assets;
import Kairo.EngineCore.AudioSceneComponents;
import Kairo.EngineCore.Scene;

namespace
{
    [[nodiscard]] kairo::assets::AudioAssetHandle TestAudioHandle()
    {
        return { kairo::assets::AssetID::Parse("12345678-1234-4234-9234-123456789abc") };
    }
}

TEST_CASE("Authored audio emitter validates and maps to mixer voice settings")
{
    using namespace kairo::engine;

    AudioEmitterComponent emitter;
    REQUIRE_THROWS_AS(emitter.Validate(), std::invalid_argument);

    emitter.Clip = TestAudioHandle();
    emitter.Loop = true;
    emitter.Spatial = true;
    emitter.Gain = 0.75;
    emitter.Pitch = 1.25;
    emitter.Attenuation.Model = AudioDistanceModel::Linear;
    emitter.Attenuation.MinDistance = 2.0;
    emitter.Attenuation.MaxDistance = 30.0;
    emitter.Attenuation.Rolloff = 0.8;
    emitter.Bus = "effects";
    emitter.Priority = 42;

    REQUIRE_NOTHROW(emitter.Validate());
    const AudioVoiceSettings voice = emitter.VoiceSettings({ 3.0, 4.0, 5.0 });
    CHECK(voice.Loop);
    CHECK(voice.Spatial);
    CHECK(voice.Gain == 0.75);
    CHECK(voice.Pitch == 1.25);
    CHECK(voice.Position.X == 3.0);
    CHECK(voice.Position.Y == 4.0);
    CHECK(voice.Position.Z == 5.0);
    CHECK(voice.Bus == "effects");
    CHECK(voice.Priority == 42);
    CHECK(voice.Attenuation.Model == AudioDistanceModel::Linear);
}

TEST_CASE("Scene audio emitter enumeration respects component and hierarchy enabled state")
{
    using namespace kairo::engine;

    Scene scene;
    const Entity parent = scene.CreateEntity("parent");
    const Entity child = scene.CreateEntity("child");
    scene.SetParent(child, parent);

    AudioEmitterComponent emitter;
    emitter.Clip = TestAudioHandle();
    scene.SetAudioEmitter(child, emitter);

    REQUIRE(scene.AudioEmitterEntities() == std::vector<Entity>{ child });
    scene.SetEnabled(parent, false);
    CHECK(scene.AudioEmitterEntities().empty());
    scene.SetEnabled(parent, true);

    scene.AudioEmitter(child).Enabled = false;
    CHECK(scene.AudioEmitterEntities().empty());
    scene.AudioEmitter(child).Enabled = true;
    REQUIRE(scene.AudioEmitterEntities() == std::vector<Entity>{ child });

    CHECK(scene.RemoveAudioEmitter(child));
    CHECK_FALSE(scene.HasAudioEmitter(child));
    CHECK_FALSE(scene.RemoveAudioEmitter(child));
}

TEST_CASE("Scene audio listener selection is deterministic and primary is unique")
{
    using namespace kairo::engine;

    Scene scene;
    const Entity first = scene.CreateEntity("first");
    const Entity second = scene.CreateEntity("second");
    const Entity third = scene.CreateEntity("third");

    scene.SetAudioListener(second, {});
    scene.SetAudioListener(first, {});
    REQUIRE(scene.ActiveAudioListener() == first);

    AudioListenerComponent primary;
    primary.Primary = true;
    scene.SetAudioListener(third, primary);
    REQUIRE(scene.ActiveAudioListener() == third);

    CHECK_THROWS_AS(scene.SetAudioListener(first, primary), std::invalid_argument);
    REQUIRE(scene.ActiveAudioListener() == third);

    scene.AudioListenerComponentFor(third).Enabled = false;
    REQUIRE(scene.ActiveAudioListener() == first);

    scene.SetEnabled(first, false);
    REQUIRE(scene.ActiveAudioListener() == second);

    scene.AudioListenerComponentFor(second).Enabled = false;
    CHECK_FALSE(scene.ActiveAudioListener().has_value());
}
