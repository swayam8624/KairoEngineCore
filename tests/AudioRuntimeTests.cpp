#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

import Kairo.EngineCore.AudioRuntime;

namespace engine = kairo::engine;
using Catch::Approx;

namespace
{
    std::shared_ptr<const engine::AudioClip> MonoClip(
        std::initializer_list<float> samples, std::uint32_t rate = 48'000u)
    {
        auto clip = std::make_shared<engine::AudioClip>();
        clip->SampleRate = rate;
        clip->Channels = 1u;
        clip->Samples.assign(samples);
        clip->Validate();
        return clip;
    }
}

TEST_CASE("Audio mixer renders mono voices to deterministic stereo",
    "[EngineCore][Audio]")
{
    engine::AudioMixer mixer(48'000u, 8u);
    const auto voice = mixer.Play(MonoClip({ 0.25f, 0.5f, -0.25f, -0.5f }));
    REQUIRE(voice != engine::InvalidAudioVoice);

    const auto output = mixer.Mix(4u);
    REQUIRE(output.size() == 8u);
    CHECK(output[0] == Approx(0.25f));
    CHECK(output[1] == Approx(0.25f));
    CHECK(output[2] == Approx(0.5f));
    CHECK(output[3] == Approx(0.5f));

    // Completion is observed on the next frame request after the last sample.
    (void)mixer.Mix(1u);
    CHECK_FALSE(mixer.IsPlaying(voice));
}

TEST_CASE("Audio buses apply mute and gain without mutating voice gain",
    "[EngineCore][Audio][Bus]")
{
    engine::AudioMixer mixer;
    mixer.DefineBus("music", { .Gain = 0.5, .Muted = false });
    const auto clip = MonoClip({ 1.0f, 1.0f, 1.0f });
    const auto voice = mixer.Play(clip, {
        .Gain = 0.5,
        .Bus = "music"
    });

    auto output = mixer.Mix(1u);
    CHECK(output[0] == Approx(0.25f));
    CHECK(output[1] == Approx(0.25f));

    mixer.SetBusState("music", { .Gain = 1.0, .Muted = true });
    output = mixer.Mix(1u);
    CHECK(output[0] == Approx(0.0f));
    CHECK(output[1] == Approx(0.0f));
    CHECK(mixer.IsPlaying(voice));
}

TEST_CASE("Spatial audio pans emitters and applies distance attenuation",
    "[EngineCore][Audio][Spatial]")
{
    engine::AudioMixer mixer;
    mixer.SetListener({
        .Position = { 0.0, 0.0, 0.0 },
        .Forward = { 0.0, 0.0, -1.0 },
        .Up = { 0.0, 1.0, 0.0 }
    });

    const auto clip = MonoClip({ 1.0f, 1.0f });
    const auto rightVoice = mixer.Play(clip, {
        .Spatial = true,
        .Position = { 1.0, 0.0, 0.0 },
        .Attenuation = {
            .Model = engine::AudioDistanceModel::Inverse,
            .MinDistance = 1.0,
            .MaxDistance = 50.0,
            .Rolloff = 1.0
        }
    });
    REQUIRE(rightVoice != engine::InvalidAudioVoice);

    const auto output = mixer.Mix(1u);
    CHECK(output[1] > output[0]);
    CHECK(output[1] > 0.9f);
    CHECK(output[0] < 0.01f);

    mixer.SetVoicePosition(rightVoice, { 10.0, 0.0, 0.0 });
    const auto distant = mixer.Mix(1u);
    CHECK(distant[1] < output[1]);
}

TEST_CASE("Looping audio wraps while one-shot voices retire",
    "[EngineCore][Audio][Loop]")
{
    engine::AudioMixer mixer(48'000u, 4u);
    const auto clip = MonoClip({ 0.2f, 0.4f });
    const auto looping = mixer.Play(clip, { .Loop = true });
    const auto oneShot = mixer.Play(clip);

    const auto output = mixer.Mix(5u);
    REQUIRE(output.size() == 10u);
    CHECK(mixer.IsPlaying(looping));
    CHECK_FALSE(mixer.IsPlaying(oneShot));
    CHECK(output[0] == Approx(0.4f));
}

TEST_CASE("Voice limit steals oldest lowest-priority voice deterministically",
    "[EngineCore][Audio][VoicePolicy]")
{
    engine::AudioMixer mixer(48'000u, 2u);
    const auto clip = MonoClip({ 0.1f, 0.1f, 0.1f });

    const auto lowOld = mixer.Play(clip, { .Loop = true, .Priority = 1 });
    const auto high = mixer.Play(clip, { .Loop = true, .Priority = 10 });
    REQUIRE(mixer.ActiveVoiceCount() == 2u);

    const auto lowerRejected = mixer.Play(clip, { .Loop = true, .Priority = 0 });
    CHECK(lowerRejected == engine::InvalidAudioVoice);
    CHECK(mixer.IsPlaying(lowOld));
    CHECK(mixer.IsPlaying(high));

    const auto replacement = mixer.Play(clip, { .Loop = true, .Priority = 1 });
    REQUIRE(replacement != engine::InvalidAudioVoice);
    CHECK_FALSE(mixer.IsPlaying(lowOld));
    CHECK(mixer.IsPlaying(high));
    CHECK(mixer.IsPlaying(replacement));
    CHECK(mixer.Stats().StolenVoices == 1u);
}

TEST_CASE("Audio validation rejects malformed runtime data",
    "[EngineCore][Audio][Validation]")
{
    engine::AudioClip clip;
    clip.Channels = 3u;
    clip.Samples = { 0.0f, 0.0f, 0.0f };
    REQUIRE_THROWS_AS(clip.Validate(), std::invalid_argument);

    engine::AudioMixer mixer;
    REQUIRE_THROWS_AS(mixer.DefineBus("master"), std::invalid_argument);
    REQUIRE_THROWS_AS(mixer.Play(MonoClip({ 0.0f }), {
        .Bus = "missing"
    }), std::invalid_argument);
}
