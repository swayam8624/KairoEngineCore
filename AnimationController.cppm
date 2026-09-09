module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.AnimationController;

import Kairo.Assets;
export import Kairo.EngineCore.AnimationRuntime;

export namespace kairo::engine
{
    using AnimationParameterValue = std::variant<bool, std::int64_t, double>;

    class AnimationParameterSet final
    {
    public:
        void Set(std::string name, AnimationParameterValue value)
        {
            ValidateName(name);
            if (const auto* number = std::get_if<double>(&value);
                number != nullptr && !std::isfinite(*number))
                throw std::invalid_argument("Animation parameter values must be finite.");
            m_Values.insert_or_assign(std::move(name), std::move(value));
        }

        void SetBool(std::string name, bool value)
        {
            Set(std::move(name), value);
        }

        void SetInteger(std::string name, std::int64_t value)
        {
            Set(std::move(name), value);
        }

        void SetFloat(std::string name, double value)
        {
            Set(std::move(name), value);
        }

        [[nodiscard]] const AnimationParameterValue* Find(std::string_view name) const
        {
            const auto found = m_Values.find(std::string(name));
            return found == m_Values.end() ? nullptr : &found->second;
        }

        [[nodiscard]] bool Contains(std::string_view name) const
        {
            return Find(name) != nullptr;
        }

        bool Erase(std::string_view name)
        {
            return m_Values.erase(std::string(name)) != 0u;
        }

        void Clear() noexcept { m_Values.clear(); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_Values.size(); }

    private:
        std::map<std::string, AnimationParameterValue> m_Values;

        static void ValidateName(std::string_view name)
        {
            if (name.empty() || name.size() > 256u)
                throw std::invalid_argument(
                    "Animation parameter names must contain 1..256 bytes.");
        }
    };

    enum class AnimationConditionOperator : std::uint8_t
    {
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual
    };

    struct AnimationTransitionCondition final
    {
        std::string Parameter;
        AnimationConditionOperator Operator = AnimationConditionOperator::Equal;
        AnimationParameterValue Value = false;

        void Validate() const
        {
            if (Parameter.empty() || Parameter.size() > 256u)
                throw std::invalid_argument(
                    "Animation transition parameter names must contain 1..256 bytes.");
            if (const auto* number = std::get_if<double>(&Value);
                number != nullptr && !std::isfinite(*number))
                throw std::invalid_argument(
                    "Animation transition numeric conditions must be finite.");
            switch (Operator)
            {
                case AnimationConditionOperator::Equal:
                case AnimationConditionOperator::NotEqual:
                case AnimationConditionOperator::Less:
                case AnimationConditionOperator::LessEqual:
                case AnimationConditionOperator::Greater:
                case AnimationConditionOperator::GreaterEqual: break;
                default:
                    throw std::invalid_argument(
                        "Animation transition condition operator is invalid.");
            }
            if (std::holds_alternative<bool>(Value) &&
                Operator != AnimationConditionOperator::Equal &&
                Operator != AnimationConditionOperator::NotEqual)
                throw std::invalid_argument(
                    "Boolean animation conditions only support equality operators.");
        }
    };

    struct AnimationControllerState final
    {
        std::string Name;
        std::size_t ClipIndex = 0u;
        float Speed = 1.0f;
        AnimationTimeMode TimeMode = AnimationTimeMode::Loop;
    };

    struct AnimationControllerTransition final
    {
        std::size_t FromState = 0u;
        std::size_t ToState = 0u;
        std::vector<AnimationTransitionCondition> Conditions;
        float BlendSeconds = 0.15f;
        float MinimumNormalizedTime = 0.0f;
        std::int32_t Priority = 0;
    };

    struct AnimationControllerDefinition final
    {
        std::vector<AnimationControllerState> States;
        std::vector<AnimationControllerTransition> Transitions;
        std::size_t EntryState = 0u;

        void Validate(const kairo::assets::GltfSceneArtifactData& scene) const
        {
            if (States.empty() || States.size() > 4096u)
                throw std::invalid_argument(
                    "Animation controller requires 1..4096 states.");
            if (EntryState >= States.size())
                throw std::out_of_range("Animation controller entry state is invalid.");

            std::set<std::string> names;
            for (const auto& state : States)
            {
                if (state.Name.empty() || state.Name.size() > 256u)
                    throw std::invalid_argument(
                        "Animation state names must contain 1..256 bytes.");
                if (!names.insert(state.Name).second)
                    throw std::invalid_argument(
                        "Animation controller state names must be unique.");
                if (state.ClipIndex >= scene.Animations.size())
                    throw std::out_of_range(
                        "Animation controller state references an invalid clip.");
                if (!std::isfinite(state.Speed) || state.Speed < 0.0f)
                    throw std::invalid_argument(
                        "Animation controller state speed must be finite and non-negative.");
                switch (state.TimeMode)
                {
                    case AnimationTimeMode::Clamp:
                    case AnimationTimeMode::Loop: break;
                    default:
                        throw std::invalid_argument(
                            "Animation controller state time mode is invalid.");
                }
            }

            if (Transitions.size() > 16'384u)
                throw std::length_error(
                    "Animation controller transition count exceeds its safety limit.");
            for (const auto& transition : Transitions)
            {
                if (transition.FromState >= States.size() ||
                    transition.ToState >= States.size())
                    throw std::out_of_range(
                        "Animation controller transition references an invalid state.");
                if (transition.FromState == transition.ToState)
                    throw std::invalid_argument(
                        "Animation controller self-transitions are not supported.");
                if (!std::isfinite(transition.BlendSeconds) ||
                    transition.BlendSeconds < 0.0f)
                    throw std::invalid_argument(
                        "Animation transition blend duration must be finite and non-negative.");
                if (!std::isfinite(transition.MinimumNormalizedTime) ||
                    transition.MinimumNormalizedTime < 0.0f ||
                    transition.MinimumNormalizedTime > 1.0f)
                    throw std::invalid_argument(
                        "Animation transition minimum normalized time must be within [0, 1].");
                if (transition.Conditions.size() > 64u)
                    throw std::length_error(
                        "Animation transition condition count exceeds its safety limit.");
                for (const auto& condition : transition.Conditions)
                    condition.Validate();
            }
        }
    };

    namespace animation_controller_detail
    {
        [[nodiscard]] inline bool IsNumeric(const AnimationParameterValue& value) noexcept
        {
            return std::holds_alternative<std::int64_t>(value) ||
                std::holds_alternative<double>(value);
        }

        [[nodiscard]] inline long double NumericValue(const AnimationParameterValue& value)
        {
            if (const auto* integer = std::get_if<std::int64_t>(&value))
                return static_cast<long double>(*integer);
            if (const auto* floating = std::get_if<double>(&value))
                return static_cast<long double>(*floating);
            throw std::invalid_argument("Animation parameter is not numeric.");
        }

        [[nodiscard]] inline bool Compare(
            const AnimationParameterValue& actual,
            const AnimationTransitionCondition& condition)
        {
            if (std::holds_alternative<bool>(actual) ||
                std::holds_alternative<bool>(condition.Value))
            {
                if (!std::holds_alternative<bool>(actual) ||
                    !std::holds_alternative<bool>(condition.Value))
                    return false;
                const bool equal = std::get<bool>(actual) ==
                    std::get<bool>(condition.Value);
                return condition.Operator == AnimationConditionOperator::Equal
                    ? equal
                    : condition.Operator == AnimationConditionOperator::NotEqual && !equal;
            }

            if (!IsNumeric(actual) || !IsNumeric(condition.Value)) return false;
            const long double left = NumericValue(actual);
            const long double right = NumericValue(condition.Value);
            switch (condition.Operator)
            {
                case AnimationConditionOperator::Equal: return left == right;
                case AnimationConditionOperator::NotEqual: return left != right;
                case AnimationConditionOperator::Less: return left < right;
                case AnimationConditionOperator::LessEqual: return left <= right;
                case AnimationConditionOperator::Greater: return left > right;
                case AnimationConditionOperator::GreaterEqual: return left >= right;
            }
            throw std::invalid_argument("Animation condition operator is invalid.");
        }
    }

    class AnimationControllerRuntime final
    {
    public:
        AnimationControllerRuntime(
            AnimationControllerDefinition definition,
            const kairo::assets::GltfSceneArtifactData& scene)
            : m_Definition(std::move(definition)),
              m_ExpectedAnimationCount(scene.Animations.size()),
              m_ExpectedNodeCount(scene.Nodes.size())
        {
            m_Definition.Validate(scene);
            Reset();
        }

        void Reset() noexcept
        {
            m_CurrentState = m_Definition.EntryState;
            m_CurrentTimeSeconds = 0.0f;
            m_Transition.reset();
        }

        [[nodiscard]] const AnimationControllerDefinition& Definition() const noexcept
        {
            return m_Definition;
        }

        [[nodiscard]] std::size_t CurrentStateIndex() const noexcept
        {
            return m_CurrentState;
        }

        [[nodiscard]] std::string_view CurrentStateName() const noexcept
        {
            return m_Definition.States[m_CurrentState].Name;
        }

        [[nodiscard]] float CurrentTimeSeconds() const noexcept
        {
            return m_CurrentTimeSeconds;
        }

        [[nodiscard]] bool IsTransitioning() const noexcept
        {
            return m_Transition.has_value();
        }

        [[nodiscard]] float TransitionAlpha() const noexcept
        {
            if (!m_Transition.has_value()) return 0.0f;
            const auto& transition =
                m_Definition.Transitions[m_Transition->DefinitionIndex];
            if (transition.BlendSeconds <= 0.0f) return 1.0f;
            return std::clamp(m_Transition->ElapsedSeconds /
                transition.BlendSeconds, 0.0f, 1.0f);
        }

        [[nodiscard]] GltfAnimationPose EvaluatePose(
            const kairo::assets::GltfSceneArtifactData& scene) const
        {
            ValidateSceneShape(scene);
            if (!m_Transition.has_value())
            {
                const auto& state = m_Definition.States[m_CurrentState];
                return SampleGltfAnimation(scene, state.ClipIndex,
                    m_CurrentTimeSeconds, state.TimeMode);
            }

            const auto& active = *m_Transition;
            const auto& transition = m_Definition.Transitions[active.DefinitionIndex];
            const auto& source = m_Definition.States[transition.FromState];
            const auto& target = m_Definition.States[transition.ToState];
            const auto sourcePose = SampleGltfAnimation(scene, source.ClipIndex,
                active.SourceTimeSeconds, source.TimeMode);
            const auto targetPose = SampleGltfAnimation(scene, target.ClipIndex,
                active.TargetTimeSeconds, target.TimeMode);
            return BlendGltfAnimationPoses(sourcePose, targetPose, TransitionAlpha());
        }

        [[nodiscard]] GltfAnimationPose Advance(
            const kairo::assets::GltfSceneArtifactData& scene,
            const AnimationParameterSet& parameters,
            float deltaSeconds)
        {
            ValidateSceneShape(scene);
            if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
                throw std::invalid_argument(
                    "Animation controller delta time must be finite and non-negative.");

            if (m_Transition.has_value())
                AdvanceTransition(deltaSeconds);
            else
            {
                const auto& state = m_Definition.States[m_CurrentState];
                m_CurrentTimeSeconds += deltaSeconds * state.Speed;
                if (!std::isfinite(m_CurrentTimeSeconds))
                    throw std::overflow_error(
                        "Animation controller state clock overflowed.");
                const auto selected = SelectTransition(scene, parameters);
                if (selected.has_value()) BeginTransition(*selected);
            }
            return EvaluatePose(scene);
        }

    private:
        struct ActiveTransition final
        {
            std::size_t DefinitionIndex = 0u;
            float SourceTimeSeconds = 0.0f;
            float TargetTimeSeconds = 0.0f;
            float ElapsedSeconds = 0.0f;
        };

        AnimationControllerDefinition m_Definition;
        std::size_t m_ExpectedAnimationCount = 0u;
        std::size_t m_ExpectedNodeCount = 0u;
        std::size_t m_CurrentState = 0u;
        float m_CurrentTimeSeconds = 0.0f;
        std::optional<ActiveTransition> m_Transition;

        void ValidateSceneShape(const kairo::assets::GltfSceneArtifactData& scene) const
        {
            if (scene.Animations.size() != m_ExpectedAnimationCount ||
                scene.Nodes.size() != m_ExpectedNodeCount)
                throw std::invalid_argument(
                    "Animation controller scene shape differs from its validated source scene.");
        }

        [[nodiscard]] float NormalizedStateProgress(
            const kairo::assets::GltfSceneArtifactData& scene) const noexcept
        {
            const auto& state = m_Definition.States[m_CurrentState];
            const float duration = scene.Animations[state.ClipIndex].DurationSeconds();
            if (duration <= 0.0f) return 1.0f;
            return std::clamp(m_CurrentTimeSeconds / duration, 0.0f, 1.0f);
        }

        [[nodiscard]] bool ConditionsPass(
            const AnimationControllerTransition& transition,
            const AnimationParameterSet& parameters) const
        {
            for (const auto& condition : transition.Conditions)
            {
                const AnimationParameterValue* actual = parameters.Find(condition.Parameter);
                if (actual == nullptr ||
                    !animation_controller_detail::Compare(*actual, condition))
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::optional<std::size_t> SelectTransition(
            const kairo::assets::GltfSceneArtifactData& scene,
            const AnimationParameterSet& parameters) const
        {
            const float progress = NormalizedStateProgress(scene);
            std::optional<std::size_t> selected;
            for (std::size_t index = 0u; index < m_Definition.Transitions.size(); ++index)
            {
                const auto& transition = m_Definition.Transitions[index];
                if (transition.FromState != m_CurrentState ||
                    progress < transition.MinimumNormalizedTime ||
                    !ConditionsPass(transition, parameters))
                    continue;
                if (!selected.has_value() || transition.Priority >
                    m_Definition.Transitions[*selected].Priority)
                    selected = index;
            }
            return selected;
        }

        void BeginTransition(std::size_t definitionIndex)
        {
            const auto& transition = m_Definition.Transitions[definitionIndex];
            if (transition.BlendSeconds == 0.0f)
            {
                m_CurrentState = transition.ToState;
                m_CurrentTimeSeconds = 0.0f;
                return;
            }
            m_Transition = ActiveTransition{
                definitionIndex,
                m_CurrentTimeSeconds,
                0.0f,
                0.0f
            };
        }

        void AdvanceTransition(float deltaSeconds)
        {
            ActiveTransition& active = *m_Transition;
            const auto& definition = m_Definition.Transitions[active.DefinitionIndex];
            const auto& source = m_Definition.States[definition.FromState];
            const auto& target = m_Definition.States[definition.ToState];
            active.SourceTimeSeconds += deltaSeconds * source.Speed;
            active.TargetTimeSeconds += deltaSeconds * target.Speed;
            active.ElapsedSeconds += deltaSeconds;
            if (!std::isfinite(active.SourceTimeSeconds) ||
                !std::isfinite(active.TargetTimeSeconds) ||
                !std::isfinite(active.ElapsedSeconds))
                throw std::overflow_error(
                    "Animation controller transition clock overflowed.");

            if (active.ElapsedSeconds >= definition.BlendSeconds)
            {
                m_CurrentState = definition.ToState;
                m_CurrentTimeSeconds = active.TargetTimeSeconds;
                m_Transition.reset();
            }
        }
    };
}
