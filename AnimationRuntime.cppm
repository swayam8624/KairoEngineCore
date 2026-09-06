module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

export module Kairo.EngineCore.AnimationRuntime;

import Kairo.Assets;
import Kairo.Foundation.Math;

export namespace kairo::engine
{
    enum class AnimationTimeMode : std::uint8_t
    {
        Clamp,
        Loop
    };

    /// One imported glTF node's evaluated local pose. TRS-authored nodes retain
    /// decomposed components for animation/blending; matrix-authored static nodes
    /// remain exact matrices and are never forced through lossy decomposition.
    struct GltfNodePose final
    {
        bool UsesTRS = false;
        kairo::foundation::math::Transformf LocalTRS{};
        kairo::foundation::math::Matrix4<float> LocalMatrix =
            kairo::foundation::math::Matrix4<float>::Identity();

        friend bool operator==(const GltfNodePose&, const GltfNodePose&) = default;
    };

    struct GltfAnimationPose final
    {
        std::vector<GltfNodePose> Nodes;

        friend bool operator==(const GltfAnimationPose&, const GltfAnimationPose&) = default;
    };

    /// glTF matrices are stored column-major; KairoMath matrices are row-major
    /// with column-vector multiplication. Transposing storage during conversion
    /// preserves the same mathematical transform and translation column.
    [[nodiscard]] inline kairo::foundation::math::Matrix4<float>
    GltfMatrixToKairo(const std::array<float, 16u>& source) noexcept
    {
        kairo::foundation::math::Matrix4<float> result;
        for (std::size_t row = 0u; row < 4u; ++row)
            for (std::size_t column = 0u; column < 4u; ++column)
                result(row, column) = source[column * 4u + row];
        return result;
    }

    [[nodiscard]] inline GltfAnimationPose BuildGltfRestPose(
        const kairo::assets::GltfSceneArtifactData& scene)
    {
        kairo::assets::ValidateGltfSceneArtifactData(scene);
        GltfAnimationPose pose;
        pose.Nodes.reserve(scene.Nodes.size());
        for (const kairo::assets::GltfNodeData& node : scene.Nodes)
        {
            GltfNodePose evaluated;
            evaluated.UsesTRS = node.HasRestTRS;
            if (node.HasRestTRS)
            {
                evaluated.LocalTRS = kairo::foundation::math::Transformf{
                    { node.RestTranslation[0], node.RestTranslation[1], node.RestTranslation[2] },
                    { node.RestRotation[0], node.RestRotation[1],
                        node.RestRotation[2], node.RestRotation[3] },
                    { node.RestScale[0], node.RestScale[1], node.RestScale[2] } };
                evaluated.LocalMatrix = kairo::foundation::math::ToMatrix4(evaluated.LocalTRS);
            }
            else
            {
                evaluated.LocalMatrix = GltfMatrixToKairo(node.LocalTransform);
            }
            pose.Nodes.push_back(evaluated);
        }
        return pose;
    }

    namespace animation_runtime_detail
    {
        [[nodiscard]] inline float NormalizeTime(
            float timeSeconds, float durationSeconds, AnimationTimeMode mode)
        {
            if (!std::isfinite(timeSeconds))
                throw std::invalid_argument("Animation sample time must be finite.");
            if (!std::isfinite(durationSeconds) || durationSeconds < 0.0f)
                throw std::invalid_argument("Animation duration must be finite and non-negative.");
            if (durationSeconds == 0.0f) return 0.0f;
            switch (mode)
            {
                case AnimationTimeMode::Clamp:
                    return std::clamp(timeSeconds, 0.0f, durationSeconds);
                case AnimationTimeMode::Loop:
                {
                    float wrapped = std::fmod(timeSeconds, durationSeconds);
                    if (wrapped < 0.0f) wrapped += durationSeconds;
                    return wrapped;
                }
            }
            throw std::invalid_argument("Animation time mode is invalid.");
        }

        struct KeySegment final
        {
            std::size_t Left = 0u;
            std::size_t Right = 0u;
            float Alpha = 0.0f;
            float DeltaSeconds = 0.0f;
        };

        [[nodiscard]] inline KeySegment FindSegment(
            const std::vector<kairo::assets::GltfAnimationKeyframe>& keys,
            float timeSeconds)
        {
            if (keys.empty())
                throw std::invalid_argument("Animation channel contains no keyframes.");
            if (timeSeconds <= keys.front().TimeSeconds) return {};
            if (timeSeconds >= keys.back().TimeSeconds)
            {
                const std::size_t last = keys.size() - 1u;
                return { last, last, 0.0f, 0.0f };
            }
            const auto upper = std::upper_bound(keys.begin(), keys.end(), timeSeconds,
                [](float time, const kairo::assets::GltfAnimationKeyframe& key)
                { return time < key.TimeSeconds; });
            const std::size_t right = static_cast<std::size_t>(upper - keys.begin());
            const std::size_t left = right - 1u;
            const float delta = keys[right].TimeSeconds - keys[left].TimeSeconds;
            return { left, right,
                (timeSeconds - keys[left].TimeSeconds) / delta, delta };
        }

        [[nodiscard]] inline std::array<float, 4u> Hermite(
            const kairo::assets::GltfAnimationKeyframe& left,
            const kairo::assets::GltfAnimationKeyframe& right,
            float alpha, float deltaSeconds)
        {
            const float a2 = alpha * alpha;
            const float a3 = a2 * alpha;
            const float h00 = 2.0f * a3 - 3.0f * a2 + 1.0f;
            const float h10 = a3 - 2.0f * a2 + alpha;
            const float h01 = -2.0f * a3 + 3.0f * a2;
            const float h11 = a3 - a2;
            std::array<float, 4u> value{};
            for (std::size_t component = 0u; component < value.size(); ++component)
            {
                value[component] = h00 * left.Value[component] +
                    h10 * deltaSeconds * left.OutTangent[component] +
                    h01 * right.Value[component] +
                    h11 * deltaSeconds * right.InTangent[component];
            }
            return value;
        }

        [[nodiscard]] inline std::array<float, 4u> SampleChannel(
            const kairo::assets::GltfAnimationChannelData& channel,
            float timeSeconds)
        {
            const KeySegment segment = FindSegment(channel.Keyframes, timeSeconds);
            const auto& left = channel.Keyframes[segment.Left];
            if (segment.Left == segment.Right ||
                channel.Interpolation == kairo::assets::GltfAnimationInterpolation::Step)
                return left.Value;
            const auto& right = channel.Keyframes[segment.Right];
            if (channel.Interpolation == kairo::assets::GltfAnimationInterpolation::CubicSpline)
            {
                auto value = Hermite(left, right, segment.Alpha, segment.DeltaSeconds);
                if (channel.Path == kairo::assets::GltfAnimationPath::Rotation)
                {
                    kairo::foundation::math::Quatf rotation{
                        value[0], value[1], value[2], value[3] };
                    rotation = rotation.Normalized();
                    value = { rotation.x, rotation.y, rotation.z, rotation.w };
                }
                return value;
            }
            if (channel.Interpolation != kairo::assets::GltfAnimationInterpolation::Linear)
                throw std::invalid_argument("Animation interpolation is unsupported.");
            if (channel.Path == kairo::assets::GltfAnimationPath::Rotation)
            {
                const kairo::foundation::math::Quatf a{
                    left.Value[0], left.Value[1], left.Value[2], left.Value[3] };
                const kairo::foundation::math::Quatf b{
                    right.Value[0], right.Value[1], right.Value[2], right.Value[3] };
                const auto rotation = kairo::foundation::math::SLerp(a, b, segment.Alpha);
                return { rotation.x, rotation.y, rotation.z, rotation.w };
            }
            std::array<float, 4u> value{};
            for (std::size_t component = 0u; component < value.size(); ++component)
                value[component] = left.Value[component] +
                    (right.Value[component] - left.Value[component]) * segment.Alpha;
            return value;
        }

        inline void ValidatePoseShape(
            const kairo::assets::GltfSceneArtifactData& scene,
            const GltfAnimationPose& pose)
        {
            if (pose.Nodes.size() != scene.Nodes.size())
                throw std::invalid_argument(
                    "Animation pose node count does not match the glTF scene.");
        }
    }

    [[nodiscard]] inline GltfAnimationPose SampleGltfAnimation(
        const kairo::assets::GltfSceneArtifactData& scene,
        std::size_t clipIndex,
        float timeSeconds,
        AnimationTimeMode mode = AnimationTimeMode::Clamp)
    {
        if (clipIndex >= scene.Animations.size())
            throw std::out_of_range("Animation clip index is out of range.");
        GltfAnimationPose pose = BuildGltfRestPose(scene);
        const auto& clip = scene.Animations[clipIndex];
        const float sampleTime = animation_runtime_detail::NormalizeTime(
            timeSeconds, clip.DurationSeconds(), mode);
        for (const auto& channel : clip.Channels)
        {
            GltfNodePose& node = pose.Nodes[channel.TargetNode];
            if (!node.UsesTRS)
                throw std::logic_error(
                    "Animation channel targeted a matrix-authored node without rest TRS.");
            const auto value = animation_runtime_detail::SampleChannel(channel, sampleTime);
            switch (channel.Path)
            {
                case kairo::assets::GltfAnimationPath::Translation:
                    node.LocalTRS.Translation = { value[0], value[1], value[2] };
                    break;
                case kairo::assets::GltfAnimationPath::Rotation:
                    node.LocalTRS.SetRotation({ value[0], value[1], value[2], value[3] });
                    break;
                case kairo::assets::GltfAnimationPath::Scale:
                    node.LocalTRS.Scale = { value[0], value[1], value[2] };
                    break;
                default:
                    throw std::invalid_argument("Animation channel path is invalid.");
            }
        }
        for (GltfNodePose& node : pose.Nodes)
            if (node.UsesTRS)
                node.LocalMatrix = kairo::foundation::math::ToMatrix4(node.LocalTRS);
        return pose;
    }

    [[nodiscard]] inline GltfAnimationPose BlendGltfAnimationPoses(
        const GltfAnimationPose& a,
        const GltfAnimationPose& b,
        float alpha)
    {
        if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f)
            throw std::invalid_argument("Animation blend alpha must be within [0, 1].");
        if (a.Nodes.size() != b.Nodes.size())
            throw std::invalid_argument("Animation poses have different node counts.");
        GltfAnimationPose result;
        result.Nodes.reserve(a.Nodes.size());
        for (std::size_t index = 0u; index < a.Nodes.size(); ++index)
        {
            const GltfNodePose& left = a.Nodes[index];
            const GltfNodePose& right = b.Nodes[index];
            if (left.UsesTRS != right.UsesTRS)
                throw std::invalid_argument("Animation poses disagree on node representation.");
            if (!left.UsesTRS)
            {
                if (left.LocalMatrix != right.LocalMatrix)
                    throw std::invalid_argument(
                        "Static matrix-authored nodes differ between animation poses.");
                result.Nodes.push_back(left);
                continue;
            }
            GltfNodePose blended;
            blended.UsesTRS = true;
            blended.LocalTRS.Translation = left.LocalTRS.Translation +
                (right.LocalTRS.Translation - left.LocalTRS.Translation) * alpha;
            blended.LocalTRS.Scale = left.LocalTRS.Scale +
                (right.LocalTRS.Scale - left.LocalTRS.Scale) * alpha;
            blended.LocalTRS.Rotation = kairo::foundation::math::SLerp(
                left.LocalTRS.Rotation, right.LocalTRS.Rotation, alpha);
            blended.LocalMatrix = kairo::foundation::math::ToMatrix4(blended.LocalTRS);
            result.Nodes.push_back(blended);
        }
        return result;
    }

    /// Resolves node-local animation output into world matrices while supporting
    /// source node arrays whose parent appears after the child. Cycle/bounds
    /// checks remain defensive even though the asset validator already rejects
    /// malformed hierarchies.
    [[nodiscard]] inline std::vector<kairo::foundation::math::Matrix4<float>>
    ResolveGltfWorldMatrices(
        const kairo::assets::GltfSceneArtifactData& scene,
        const GltfAnimationPose& pose)
    {
        animation_runtime_detail::ValidatePoseShape(scene, pose);
        std::vector<kairo::foundation::math::Matrix4<float>> world(
            scene.Nodes.size(), kairo::foundation::math::Matrix4<float>::Identity());
        std::vector<std::uint8_t> state(scene.Nodes.size(), 0u);
        std::function<void(std::size_t)> resolve = [&](std::size_t index)
        {
            if (state[index] == 2u) return;
            if (state[index] == 1u)
                throw std::invalid_argument("glTF node hierarchy contains a cycle.");
            state[index] = 1u;
            const std::int32_t parent = scene.Nodes[index].Parent;
            if (parent >= 0)
            {
                const std::size_t parentIndex = static_cast<std::size_t>(parent);
                if (parentIndex >= scene.Nodes.size())
                    throw std::out_of_range("glTF node parent index is invalid.");
                resolve(parentIndex);
                world[index] = world[parentIndex] * pose.Nodes[index].LocalMatrix;
            }
            else world[index] = pose.Nodes[index].LocalMatrix;
            state[index] = 2u;
        };
        for (std::size_t index = 0u; index < scene.Nodes.size(); ++index) resolve(index);
        return world;
    }
}