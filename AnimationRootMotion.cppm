module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

export module Kairo.EngineCore.AnimationRootMotion;

import Kairo.Assets;
import Kairo.Foundation.Math;
export import Kairo.EngineCore.AnimationRuntime;

export namespace kairo::engine
{
    /// Selects which authored motion channels are exported from a designated
    /// glTF motion node. Scale is intentionally never exported as gameplay root
    /// motion; it remains part of the evaluated skeleton pose.
    struct GltfRootMotionSettings final
    {
        bool TranslationX = true;
        bool TranslationY = true;
        bool TranslationZ = true;
        bool Rotation = true;
    };

    /// A frame-to-frame motion delta in the designated motion node's parent
    /// coordinate space. Translation is an additive displacement. Rotation is
    /// the relative orientation change from the previous sample to the current
    /// sample. The caller may apply this delta to an actor/world transform using
    /// the gameplay coordinate-space convention appropriate to that actor.
    struct GltfRootMotionDelta final
    {
        kairo::foundation::math::Vector3<float> Translation{};
        kairo::foundation::math::Quatf Rotation =
            kairo::foundation::math::Quatf::Identity();

        [[nodiscard]] bool IsIdentity(float epsilon = 1.0e-6f) const noexcept
        {
            return std::abs(Translation.x) <= epsilon &&
                std::abs(Translation.y) <= epsilon &&
                std::abs(Translation.z) <= epsilon &&
                kairo::foundation::math::NearlyEqual(
                    Rotation,
                    kairo::foundation::math::Quatf::Identity(),
                    epsilon);
        }
    };

    namespace animation_root_motion_detail
    {
        using Transform = kairo::foundation::math::Transformf;
        using Quaternion = kairo::foundation::math::Quatf;

        inline void ValidateRequest(
            const kairo::assets::GltfSceneArtifactData& scene,
            std::size_t clipIndex,
            std::size_t motionNodeIndex,
            float previousTimeSeconds,
            float currentTimeSeconds)
        {
            if (clipIndex >= scene.Animations.size())
                throw std::out_of_range("Root-motion clip index is out of range.");
            if (motionNodeIndex >= scene.Nodes.size())
                throw std::out_of_range("Root-motion node index is out of range.");
            if (!scene.Nodes[motionNodeIndex].HasRestTRS)
                throw std::invalid_argument(
                    "Root motion requires a TRS-authored glTF motion node.");
            if (!std::isfinite(previousTimeSeconds) ||
                !std::isfinite(currentTimeSeconds) ||
                previousTimeSeconds < 0.0f ||
                currentTimeSeconds < previousTimeSeconds)
                throw std::invalid_argument(
                    "Root-motion sample times must be finite, non-negative, and monotonic.");
        }

        [[nodiscard]] inline Transform SampleTransform(
            const kairo::assets::GltfSceneArtifactData& scene,
            std::size_t clipIndex,
            std::size_t motionNodeIndex,
            float timeSeconds,
            AnimationTimeMode mode)
        {
            const auto pose = SampleGltfAnimation(scene, clipIndex, timeSeconds, mode);
            const auto& node = pose.Nodes[motionNodeIndex];
            if (!node.UsesTRS)
                throw std::logic_error(
                    "Validated root-motion node unexpectedly lost TRS representation.");
            return node.LocalTRS;
        }

        [[nodiscard]] inline GltfRootMotionDelta DeltaBetween(
            const Transform& from,
            const Transform& to,
            const GltfRootMotionSettings& settings)
        {
            GltfRootMotionDelta result;
            const auto translation = to.Translation - from.Translation;
            result.Translation = {
                settings.TranslationX ? translation.x : 0.0f,
                settings.TranslationY ? translation.y : 0.0f,
                settings.TranslationZ ? translation.z : 0.0f
            };
            if (settings.Rotation)
            {
                result.Rotation = (to.Rotation *
                    kairo::foundation::math::Inverse(from.Rotation)).Normalized();
            }
            return result;
        }

        [[nodiscard]] inline GltfRootMotionDelta ComposeParentSpace(
            const GltfRootMotionDelta& first,
            const GltfRootMotionDelta& second)
        {
            GltfRootMotionDelta result;
            // The extracted translations are expressed in the animation motion
            // node's parent space, so consecutive clip segments add directly.
            // Relative rotations compose chronologically: first, then second.
            result.Translation = first.Translation + second.Translation;
            result.Rotation = (second.Rotation * first.Rotation).Normalized();
            return result;
        }

        [[nodiscard]] inline Quaternion QuaternionPower(
            Quaternion base,
            std::uint64_t exponent) noexcept
        {
            Quaternion result = Quaternion::Identity();
            base = base.Normalized();
            // Exponentiation by squaring keeps large clock jumps logarithmic in
            // the number of skipped loops rather than proportional to them.
            while (exponent != 0u)
            {
                if ((exponent & 1u) != 0u)
                    result = (base * result).Normalized();
                exponent >>= 1u;
                if (exponent != 0u)
                    base = (base * base).Normalized();
            }
            return result;
        }

        [[nodiscard]] inline float ScaledTranslationComponent(
            float component,
            std::uint64_t repetitions)
        {
            const long double scaled = static_cast<long double>(component) *
                static_cast<long double>(repetitions);
            constexpr long double maximum =
                static_cast<long double>(std::numeric_limits<float>::max());
            if (!std::isfinite(scaled) || scaled > maximum || scaled < -maximum)
                throw std::overflow_error(
                    "Repeated root-motion translation exceeds float range.");
            return static_cast<float>(scaled);
        }

        [[nodiscard]] inline GltfRootMotionDelta RepeatParentSpace(
            const GltfRootMotionDelta& delta,
            std::uint64_t repetitions)
        {
            if (repetitions == 0u) return {};
            GltfRootMotionDelta result;
            result.Translation = {
                ScaledTranslationComponent(delta.Translation.x, repetitions),
                ScaledTranslationComponent(delta.Translation.y, repetitions),
                ScaledTranslationComponent(delta.Translation.z, repetitions)
            };
            result.Rotation = QuaternionPower(delta.Rotation, repetitions);
            return result;
        }

        [[nodiscard]] inline std::uint64_t CycleIndex(
            float timeSeconds,
            float durationSeconds)
        {
            const long double cycles = std::floor(
                static_cast<long double>(timeSeconds) /
                static_cast<long double>(durationSeconds));
            constexpr long double maximum = static_cast<long double>(
                std::numeric_limits<std::uint64_t>::max());
            if (!std::isfinite(cycles) || cycles < 0.0L || cycles > maximum)
                throw std::overflow_error(
                    "Root-motion animation clock exceeds supported loop count.");
            return static_cast<std::uint64_t>(cycles);
        }

        [[nodiscard]] inline bool IsExactPositiveMultiple(
            float value,
            float duration) noexcept
        {
            if (value <= 0.0f || duration <= 0.0f) return false;
            const float remainder = std::fmod(value, duration);
            const float tolerance = std::max(1.0e-6f, duration * 1.0e-6f);
            return std::abs(remainder) <= tolerance ||
                std::abs(remainder - duration) <= tolerance;
        }
    }

    /// Extracts root motion between two monotonically increasing, unwrapped
    /// animation times. Loop mode is boundary-safe: crossing one or many clip
    /// wraps accumulates the end-of-clip and start-of-clip segments instead of
    /// producing the large backwards jump caused by simply subtracting wrapped
    /// samples.
    [[nodiscard]] inline GltfRootMotionDelta ExtractGltfRootMotion(
        const kairo::assets::GltfSceneArtifactData& scene,
        std::size_t clipIndex,
        std::size_t motionNodeIndex,
        float previousTimeSeconds,
        float currentTimeSeconds,
        AnimationTimeMode mode = AnimationTimeMode::Loop,
        GltfRootMotionSettings settings = {})
    {
        animation_root_motion_detail::ValidateRequest(
            scene, clipIndex, motionNodeIndex,
            previousTimeSeconds, currentTimeSeconds);
        if (previousTimeSeconds == currentTimeSeconds)
            return {};

        const float duration = scene.Animations[clipIndex].DurationSeconds();
        if (!std::isfinite(duration) || duration < 0.0f)
            throw std::invalid_argument(
                "Root-motion clip duration must be finite and non-negative.");

        if (mode == AnimationTimeMode::Clamp || duration == 0.0f)
        {
            const auto from = animation_root_motion_detail::SampleTransform(
                scene, clipIndex, motionNodeIndex,
                previousTimeSeconds, AnimationTimeMode::Clamp);
            const auto to = animation_root_motion_detail::SampleTransform(
                scene, clipIndex, motionNodeIndex,
                currentTimeSeconds, AnimationTimeMode::Clamp);
            return animation_root_motion_detail::DeltaBetween(from, to, settings);
        }
        if (mode != AnimationTimeMode::Loop)
            throw std::invalid_argument("Root-motion time mode is invalid.");

        std::uint64_t previousCycle = animation_root_motion_detail::CycleIndex(
            previousTimeSeconds, duration);
        std::uint64_t currentCycle = animation_root_motion_detail::CycleIndex(
            currentTimeSeconds, duration);
        float previousLocal = std::fmod(previousTimeSeconds, duration);
        float currentLocal = std::fmod(currentTimeSeconds, duration);
        if (previousLocal < 0.0f) previousLocal += duration;
        if (currentLocal < 0.0f) currentLocal += duration;

        // Treat an exact positive end time as the end of the preceding cycle,
        // while an exact previous time remains the start of its current cycle.
        // This makes [1.5, 2.0] yield the final half-second of a 2s clip rather
        // than an artificial wrap back to sample zero.
        if (currentTimeSeconds > previousTimeSeconds && currentCycle > 0u &&
            animation_root_motion_detail::IsExactPositiveMultiple(
                currentTimeSeconds, duration))
        {
            --currentCycle;
            currentLocal = duration;
        }

        if (currentCycle == previousCycle)
        {
            const auto from = animation_root_motion_detail::SampleTransform(
                scene, clipIndex, motionNodeIndex,
                previousLocal, AnimationTimeMode::Clamp);
            const auto to = animation_root_motion_detail::SampleTransform(
                scene, clipIndex, motionNodeIndex,
                currentLocal, AnimationTimeMode::Clamp);
            return animation_root_motion_detail::DeltaBetween(from, to, settings);
        }
        if (currentCycle < previousCycle)
            throw std::logic_error(
                "Root-motion loop cycle accounting became non-monotonic.");

        const auto clipStart = animation_root_motion_detail::SampleTransform(
            scene, clipIndex, motionNodeIndex, 0.0f, AnimationTimeMode::Clamp);
        const auto clipEnd = animation_root_motion_detail::SampleTransform(
            scene, clipIndex, motionNodeIndex, duration, AnimationTimeMode::Clamp);
        const auto from = animation_root_motion_detail::SampleTransform(
            scene, clipIndex, motionNodeIndex,
            previousLocal, AnimationTimeMode::Clamp);
        const auto to = animation_root_motion_detail::SampleTransform(
            scene, clipIndex, motionNodeIndex,
            currentLocal, AnimationTimeMode::Clamp);

        GltfRootMotionDelta accumulated =
            animation_root_motion_detail::DeltaBetween(from, clipEnd, settings);
        const GltfRootMotionDelta fullCycle =
            animation_root_motion_detail::DeltaBetween(clipStart, clipEnd, settings);

        const std::uint64_t completeMiddleCycles =
            currentCycle - previousCycle - 1u;
        accumulated = animation_root_motion_detail::ComposeParentSpace(
            accumulated,
            animation_root_motion_detail::RepeatParentSpace(
                fullCycle, completeMiddleCycles));
        accumulated = animation_root_motion_detail::ComposeParentSpace(
            accumulated,
            animation_root_motion_detail::DeltaBetween(clipStart, to, settings));
        return accumulated;
    }

    /// Removes the selected root-motion channels from an already evaluated pose
    /// by restoring the designated motion node's authored rest values. This is
    /// the standard second half of root-motion playback: apply the extracted
    /// delta to gameplay/world motion, then keep the skeleton centered so the
    /// same motion is not rendered twice.
    inline void RemoveGltfRootMotionFromPose(
        const kairo::assets::GltfSceneArtifactData& scene,
        std::size_t motionNodeIndex,
        GltfAnimationPose& pose,
        GltfRootMotionSettings settings = {})
    {
        if (motionNodeIndex >= scene.Nodes.size() ||
            motionNodeIndex >= pose.Nodes.size())
            throw std::out_of_range("Root-motion removal node index is out of range.");
        if (pose.Nodes.size() != scene.Nodes.size())
            throw std::invalid_argument(
                "Root-motion removal pose node count does not match the glTF scene.");
        const auto& source = scene.Nodes[motionNodeIndex];
        auto& evaluated = pose.Nodes[motionNodeIndex];
        if (!source.HasRestTRS || !evaluated.UsesTRS)
            throw std::invalid_argument(
                "Root-motion removal requires a TRS-authored motion node.");

        if (settings.TranslationX)
            evaluated.LocalTRS.Translation.x = source.RestTranslation[0];
        if (settings.TranslationY)
            evaluated.LocalTRS.Translation.y = source.RestTranslation[1];
        if (settings.TranslationZ)
            evaluated.LocalTRS.Translation.z = source.RestTranslation[2];
        if (settings.Rotation)
        {
            evaluated.LocalTRS.SetRotation({
                source.RestRotation[0], source.RestRotation[1],
                source.RestRotation[2], source.RestRotation[3] });
        }
        evaluated.LocalMatrix =
            kairo::foundation::math::ToMatrix4(evaluated.LocalTRS);
    }
}
