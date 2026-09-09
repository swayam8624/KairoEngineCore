module;

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

export module Kairo.EngineCore.AudioSceneComponents;

import Kairo.Assets;
import Kairo.EngineCore.AudioRuntime;

export namespace kairo::engine
{
    /// Renderer/platform-neutral authored sound source. The persistent asset
    /// identity survives path moves; decoding remains KairoAssets-owned and
    /// playback/mixing remains AudioRuntime-owned.
    struct AudioEmitterComponent final
    {
        kairo::assets::AudioAssetHandle Clip;
        bool Enabled = true;
        bool PlayOnStart = true;
        bool Loop = false;
        bool Spatial = true;
        double Gain = 1.0;
        double Pitch = 1.0;
        AudioAttenuation Attenuation{};
        std::string Bus = "master";
        std::int32_t Priority = 0;

        void Validate() const
        {
            if (!Clip.IsValid())
                throw std::invalid_argument(
                    "AudioEmitterComponent requires a valid audio asset handle.");
            AudioVoiceSettings settings;
            settings.Gain = Gain;
            settings.Pitch = Pitch;
            settings.Loop = Loop;
            settings.Spatial = Spatial;
            settings.Attenuation = Attenuation;
            settings.Bus = Bus;
            settings.Priority = Priority;
            settings.Validate();
        }

        /// Converts authored playback policy into the mixer contract. World
        /// position is supplied by the runtime adapter because scene transforms
        /// are hierarchy-derived and can change every frame.
        [[nodiscard]] AudioVoiceSettings VoiceSettings(AudioVec3 worldPosition = {}) const
        {
            Validate();
            AudioVoiceSettings settings;
            settings.Gain = Gain;
            settings.Pitch = Pitch;
            settings.Loop = Loop;
            settings.Spatial = Spatial;
            settings.Position = worldPosition;
            settings.Attenuation = Attenuation;
            settings.Bus = Bus;
            settings.Priority = Priority;
            settings.Validate();
            return settings;
        }
    };

    /// Marks one authored scene entity as a listener transform source. The
    /// component intentionally stores no duplicate position/orientation: those
    /// values come from Scene::WorldTransform so hierarchy edits and gameplay
    /// movement remain authoritative.
    struct AudioListenerComponent final
    {
        bool Enabled = true;
        bool Primary = false;

        void Validate() const noexcept {}
    };
}
