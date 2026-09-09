module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.EngineCore.AudioRuntime;

export namespace kairo::engine
{
    struct AudioVec3 final
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;

        [[nodiscard]] bool IsFinite() const noexcept
        {
            return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z);
        }
    };

    [[nodiscard]] inline AudioVec3 operator-(AudioVec3 a, AudioVec3 b) noexcept
    {
        return { a.X - b.X, a.Y - b.Y, a.Z - b.Z };
    }

    [[nodiscard]] inline double AudioDot(AudioVec3 a, AudioVec3 b) noexcept
    {
        return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    }

    [[nodiscard]] inline AudioVec3 AudioCross(AudioVec3 a, AudioVec3 b) noexcept
    {
        return {
            a.Y * b.Z - a.Z * b.Y,
            a.Z * b.X - a.X * b.Z,
            a.X * b.Y - a.Y * b.X
        };
    }

    [[nodiscard]] inline double AudioLength(AudioVec3 value) noexcept
    {
        return std::sqrt(AudioDot(value, value));
    }

    [[nodiscard]] inline AudioVec3 AudioNormalize(AudioVec3 value)
    {
        if (!value.IsFinite())
            throw std::invalid_argument("Audio direction must be finite.");
        const double length = AudioLength(value);
        if (!std::isfinite(length) || length <= 1.0e-9)
            throw std::invalid_argument("Audio direction must be non-zero.");
        return { value.X / length, value.Y / length, value.Z / length };
    }

    /// Interleaved immutable PCM clip in normalized floating-point form.
    /// Keeping decoding outside the mixer lets KairoAssets own source formats
    /// while every platform audio backend consumes the same runtime contract.
    struct AudioClip final
    {
        std::uint32_t SampleRate = 48'000u;
        std::uint32_t Channels = 1u;
        std::vector<float> Samples;

        void Validate() const
        {
            if (SampleRate < 8'000u || SampleRate > 384'000u)
                throw std::invalid_argument("Audio sample rate is outside the supported range.");
            if (Channels != 1u && Channels != 2u)
                throw std::invalid_argument("Audio runtime clips support mono or stereo PCM.");
            if (Samples.empty() || Samples.size() % Channels != 0u)
                throw std::invalid_argument("Audio PCM sample count is invalid for its channel count.");
            for (const float sample : Samples)
                if (!std::isfinite(sample))
                    throw std::invalid_argument("Audio PCM samples must be finite.");
        }

        [[nodiscard]] std::size_t FrameCount() const noexcept
        {
            return Channels == 0u ? 0u : Samples.size() / Channels;
        }

        [[nodiscard]] double DurationSeconds() const noexcept
        {
            return SampleRate == 0u ? 0.0 :
                static_cast<double>(FrameCount()) / static_cast<double>(SampleRate);
        }
    };

    enum class AudioDistanceModel : std::uint8_t
    {
        Linear,
        Inverse
    };

    struct AudioAttenuation final
    {
        AudioDistanceModel Model = AudioDistanceModel::Inverse;
        double MinDistance = 1.0;
        double MaxDistance = 100.0;
        double Rolloff = 1.0;

        void Validate() const
        {
            if (!std::isfinite(MinDistance) || !std::isfinite(MaxDistance) ||
                !std::isfinite(Rolloff) || MinDistance <= 0.0 ||
                MaxDistance < MinDistance || Rolloff < 0.0)
                throw std::invalid_argument("Audio attenuation settings are invalid.");
        }

        [[nodiscard]] double GainAt(double distance) const
        {
            Validate();
            if (!std::isfinite(distance) || distance < 0.0)
                throw std::invalid_argument("Audio distance must be finite and non-negative.");
            if (distance <= MinDistance) return 1.0;
            if (distance >= MaxDistance) return 0.0;
            if (Model == AudioDistanceModel::Linear)
            {
                const double normalized = (distance - MinDistance) /
                    (MaxDistance - MinDistance);
                return std::clamp(1.0 - Rolloff * normalized, 0.0, 1.0);
            }
            const double normalized = distance / MinDistance;
            return std::clamp(1.0 / (1.0 + Rolloff * (normalized - 1.0)),
                0.0, 1.0);
        }
    };

    struct AudioListener final
    {
        AudioVec3 Position{};
        AudioVec3 Forward{ 0.0, 0.0, -1.0 };
        AudioVec3 Up{ 0.0, 1.0, 0.0 };

        void Validate() const
        {
            if (!Position.IsFinite())
                throw std::invalid_argument("Audio listener position must be finite.");
            const auto forward = AudioNormalize(Forward);
            const auto up = AudioNormalize(Up);
            if (AudioLength(AudioCross(forward, up)) <= 1.0e-6)
                throw std::invalid_argument("Audio listener forward and up cannot be parallel.");
        }
    };

    struct AudioVoiceHandle final
    {
        std::uint64_t Value = 0u;
        friend constexpr bool operator==(const AudioVoiceHandle&,
            const AudioVoiceHandle&) noexcept = default;
    };

    inline constexpr AudioVoiceHandle InvalidAudioVoice{};

    struct AudioVoiceSettings final
    {
        double Gain = 1.0;
        double Pitch = 1.0;
        bool Loop = false;
        bool Spatial = false;
        AudioVec3 Position{};
        AudioAttenuation Attenuation{};
        std::string Bus = "master";
        std::int32_t Priority = 0;

        void Validate() const
        {
            if (!std::isfinite(Gain) || Gain < 0.0 || Gain > 16.0 ||
                !std::isfinite(Pitch) || Pitch < 0.125 || Pitch > 8.0 ||
                !Position.IsFinite() || Bus.empty() || Bus.size() > 128u)
                throw std::invalid_argument("Audio voice settings are invalid.");
            Attenuation.Validate();
        }
    };

    struct AudioBusState final
    {
        double Gain = 1.0;
        bool Muted = false;

        void Validate() const
        {
            if (!std::isfinite(Gain) || Gain < 0.0 || Gain > 16.0)
                throw std::invalid_argument("Audio bus gain is invalid.");
        }
    };

    struct AudioMixStats final
    {
        std::uint64_t MixCalls = 0u;
        std::uint64_t OutputFrames = 0u;
        std::uint64_t VoiceFrames = 0u;
        std::uint64_t StolenVoices = 0u;
        std::uint64_t ClippedSamples = 0u;
        std::size_t PeakVoices = 0u;
    };

    /// CPU reference mixer shared by Player, editor preview, tests, and future
    /// platform output adapters. Native APIs only need to consume interleaved
    /// stereo float frames; voice policy and spatial semantics stay identical.
    class AudioMixer final
    {
        struct Voice final
        {
            std::shared_ptr<const AudioClip> Clip;
            AudioVoiceSettings Settings;
            double CursorFrames = 0.0;
            bool Paused = false;
        };

    public:
        explicit AudioMixer(std::uint32_t outputSampleRate = 48'000u,
            std::size_t maximumVoices = 128u)
            : m_OutputSampleRate(outputSampleRate),
              m_MaximumVoices(maximumVoices)
        {
            if (outputSampleRate < 8'000u || outputSampleRate > 384'000u ||
                maximumVoices == 0u || maximumVoices > 16'384u)
                throw std::invalid_argument("Audio mixer configuration is invalid.");
            m_Buses.emplace("master", AudioBusState{});
        }

        void SetListener(AudioListener listener)
        {
            listener.Validate();
            m_Listener = listener;
        }

        [[nodiscard]] const AudioListener& Listener() const noexcept
        {
            return m_Listener;
        }

        void DefineBus(std::string name, AudioBusState state = {})
        {
            ValidateBusName(name);
            state.Validate();
            if (!m_Buses.emplace(std::move(name), state).second)
                throw std::invalid_argument("Audio bus is already defined.");
        }

        void SetBusState(std::string_view name, AudioBusState state)
        {
            state.Validate();
            auto found = m_Buses.find(name);
            if (found == m_Buses.end())
                throw std::out_of_range("Audio bus does not exist.");
            found->second = state;
        }

        [[nodiscard]] const AudioBusState& Bus(std::string_view name) const
        {
            const auto found = m_Buses.find(name);
            if (found == m_Buses.end())
                throw std::out_of_range("Audio bus does not exist.");
            return found->second;
        }

        [[nodiscard]] AudioVoiceHandle Play(std::shared_ptr<const AudioClip> clip,
            AudioVoiceSettings settings = {})
        {
            if (!clip) throw std::invalid_argument("Audio playback requires a clip.");
            clip->Validate();
            settings.Validate();
            if (!m_Buses.contains(settings.Bus))
                throw std::invalid_argument("Audio voice references an undefined bus.");

            if (m_Voices.size() >= m_MaximumVoices)
            {
                const auto victim = SelectVoiceToSteal();
                if (victim == m_Voices.end() ||
                    settings.Priority < victim->second.Settings.Priority)
                    return InvalidAudioVoice;
                m_Voices.erase(victim);
                ++m_Stats.StolenVoices;
            }

            if (m_NextVoice == 0u)
                throw std::overflow_error("Audio voice ID space exhausted.");
            const AudioVoiceHandle handle{ m_NextVoice++ };
            m_Voices.emplace(handle.Value,
                Voice{ std::move(clip), std::move(settings) });
            m_Stats.PeakVoices = std::max(m_Stats.PeakVoices, m_Voices.size());
            return handle;
        }

        bool Stop(AudioVoiceHandle handle) noexcept
        {
            return handle.Value != 0u && m_Voices.erase(handle.Value) != 0u;
        }

        void SetPaused(AudioVoiceHandle handle, bool paused)
        {
            RequireVoice(handle).Paused = paused;
        }

        void SetVoicePosition(AudioVoiceHandle handle, AudioVec3 position)
        {
            if (!position.IsFinite())
                throw std::invalid_argument("Audio voice position must be finite.");
            RequireVoice(handle).Settings.Position = position;
        }

        void SetVoiceGain(AudioVoiceHandle handle, double gain)
        {
            if (!std::isfinite(gain) || gain < 0.0 || gain > 16.0)
                throw std::invalid_argument("Audio voice gain is invalid.");
            RequireVoice(handle).Settings.Gain = gain;
        }

        [[nodiscard]] bool IsPlaying(AudioVoiceHandle handle) const noexcept
        {
            return handle.Value != 0u && m_Voices.contains(handle.Value);
        }

        [[nodiscard]] std::size_t ActiveVoiceCount() const noexcept
        {
            return m_Voices.size();
        }

        [[nodiscard]] const AudioMixStats& Stats() const noexcept { return m_Stats; }

        /// Returns interleaved stereo floating-point frames in [-1,1].
        [[nodiscard]] std::vector<float> Mix(std::size_t frameCount)
        {
            if (frameCount > 1'048'576u)
                throw std::length_error("Audio mix request exceeds the frame budget.");
            std::vector<float> output(frameCount * 2u, 0.0f);
            std::vector<std::uint64_t> completed;
            ++m_Stats.MixCalls;
            m_Stats.OutputFrames += frameCount;

            for (auto& [id, voice] : m_Voices)
            {
                if (voice.Paused) continue;
                const auto& bus = Bus(voice.Settings.Bus);
                const auto& master = Bus("master");
                const double busGain = (bus.Muted || master.Muted)
                    ? 0.0 : bus.Gain * (voice.Settings.Bus == "master" ? 1.0 : master.Gain);
                const double gain = voice.Settings.Gain * busGain;
                const double step = voice.Settings.Pitch *
                    static_cast<double>(voice.Clip->SampleRate) /
                    static_cast<double>(m_OutputSampleRate);

                double leftSpatial = 1.0;
                double rightSpatial = 1.0;
                if (voice.Settings.Spatial)
                    ComputeSpatialGains(voice.Settings, leftSpatial, rightSpatial);

                bool ended = false;
                for (std::size_t frame = 0u; frame < frameCount; ++frame)
                {
                    if (!NormalizeCursor(voice))
                    {
                        ended = true;
                        break;
                    }
                    const auto sample = SampleVoice(voice);
                    output[frame * 2u] += static_cast<float>(
                        static_cast<double>(sample.first) * gain * leftSpatial);
                    output[frame * 2u + 1u] += static_cast<float>(
                        static_cast<double>(sample.second) * gain * rightSpatial);
                    voice.CursorFrames += step;
                    ++m_Stats.VoiceFrames;
                }
                if (ended) completed.push_back(id);
            }

            for (const auto id : completed) m_Voices.erase(id);
            for (float& sample : output)
            {
                if (!std::isfinite(sample))
                    throw std::runtime_error("Audio mixer produced a non-finite sample.");
                if (sample > 1.0f || sample < -1.0f) ++m_Stats.ClippedSamples;
                sample = std::clamp(sample, -1.0f, 1.0f);
            }
            return output;
        }

    private:
        std::uint32_t m_OutputSampleRate;
        std::size_t m_MaximumVoices;
        std::uint64_t m_NextVoice = 1u;
        AudioListener m_Listener{};
        std::map<std::string, AudioBusState, std::less<>> m_Buses;
        std::map<std::uint64_t, Voice> m_Voices;
        AudioMixStats m_Stats{};

        static void ValidateBusName(std::string_view name)
        {
            if (name.empty() || name.size() > 128u || name == "master")
                throw std::invalid_argument("Audio bus name is invalid or reserved.");
        }

        [[nodiscard]] Voice& RequireVoice(AudioVoiceHandle handle)
        {
            if (handle.Value == 0u)
                throw std::invalid_argument("Audio voice handle is invalid.");
            const auto found = m_Voices.find(handle.Value);
            if (found == m_Voices.end())
                throw std::out_of_range("Audio voice does not exist.");
            return found->second;
        }

        [[nodiscard]] std::map<std::uint64_t, Voice>::iterator SelectVoiceToSteal()
        {
            return std::min_element(m_Voices.begin(), m_Voices.end(),
                [](const auto& a, const auto& b)
                {
                    if (a.second.Settings.Priority != b.second.Settings.Priority)
                        return a.second.Settings.Priority < b.second.Settings.Priority;
                    return a.first < b.first;
                });
        }

        [[nodiscard]] bool NormalizeCursor(Voice& voice) const
        {
            const double frames = static_cast<double>(voice.Clip->FrameCount());
            if (voice.CursorFrames < frames) return true;
            if (!voice.Settings.Loop) return false;
            voice.CursorFrames = std::fmod(voice.CursorFrames, frames);
            return true;
        }

        [[nodiscard]] std::pair<float, float> SampleVoice(const Voice& voice) const
        {
            const auto frameCount = voice.Clip->FrameCount();
            const std::size_t a = std::min(
                static_cast<std::size_t>(voice.CursorFrames), frameCount - 1u);
            std::size_t b = a + 1u;
            if (b >= frameCount) b = voice.Settings.Loop ? 0u : a;
            const double fraction = voice.CursorFrames - static_cast<double>(a);

            const auto channel = [&](std::size_t frame, std::uint32_t index)
            {
                if (voice.Clip->Channels == 1u)
                    return voice.Clip->Samples[frame];
                return voice.Clip->Samples[frame * 2u + index];
            };
            const auto lerp = [fraction](float x, float y)
            {
                return static_cast<float>(static_cast<double>(x) +
                    (static_cast<double>(y) - static_cast<double>(x)) * fraction);
            };

            float left = lerp(channel(a, 0u), channel(b, 0u));
            float right = voice.Clip->Channels == 1u
                ? left : lerp(channel(a, 1u), channel(b, 1u));
            if (voice.Settings.Spatial)
            {
                const float mono = 0.5f * (left + right);
                left = mono;
                right = mono;
            }
            return { left, right };
        }

        void ComputeSpatialGains(const AudioVoiceSettings& settings,
            double& left, double& right) const
        {
            const auto delta = settings.Position - m_Listener.Position;
            const double distance = AudioLength(delta);
            const double attenuation = settings.Attenuation.GainAt(distance);
            if (distance <= 1.0e-9)
            {
                left = attenuation;
                right = attenuation;
                return;
            }

            const auto direction = AudioNormalize(delta);
            const auto forward = AudioNormalize(m_Listener.Forward);
            const auto up = AudioNormalize(m_Listener.Up);
            const auto listenerRight = AudioNormalize(AudioCross(forward, up));
            const double pan = std::clamp(AudioDot(direction, listenerRight), -1.0, 1.0);
            // Equal-power panning maintains approximately constant perceived
            // loudness while the emitter crosses the listener.
            left = attenuation * std::sqrt(0.5 * (1.0 - pan));
            right = attenuation * std::sqrt(0.5 * (1.0 + pan));
        }
    };
}
