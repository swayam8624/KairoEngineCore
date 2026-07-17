module;

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

export module Kairo.EngineCore.InputMap;

import Kairo.EngineCore.Input;
import Kairo.EngineCore.TextFormat;
import Kairo.EngineCore.TextValidation;

export namespace kairo::engine
{
    enum class InputActionValueType : std::uint8_t { Button, Axis1D, Axis2D };
    enum class InputBindingDevice : std::uint8_t { Keyboard, MouseButton, GamepadButton, GamepadAxis };

    struct InputActionDefinition final
    {
        std::string Name;
        InputActionValueType Type = InputActionValueType::Button;
        friend bool operator==(const InputActionDefinition&, const InputActionDefinition&) = default;
    };

    /// Scale maps one digital/analog control sample into action space. Button
    /// bindings commonly use {1,0}; WASD uses cardinal 2D vectors; analog axes
    /// use one cardinal direction and preserve signed source values.
    struct InputBinding final
    {
        std::string Action;
        InputBindingDevice Device = InputBindingDevice::Keyboard;
        std::int32_t Code = 0;
        InputVector2 Scale{ 1.0f, 0.0f };
        float DeadZone = 0.0f;
        friend bool operator==(const InputBinding&, const InputBinding&) = default;
    };

    struct InputActionState final
    {
        InputVector2 Value{};
        bool Down = false;
        bool Pressed = false;
        bool Released = false;
    };

    class InputMapFormatError final : public std::runtime_error
    {
    public:
        InputMapFormatError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("Kairo input map " + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}
        std::size_t Line;
        std::size_t Column;
    };

    class InputActionMap final
    {
    public:
        static constexpr std::size_t MaximumActions = 256u;
        static constexpr std::size_t MaximumBindings = 2048u;

        void AddAction(InputActionDefinition action)
        {
            ValidateUtf8Text(action.Name, { 1u, 64u, false, false }, "Input action name");
            if (m_Actions.size() >= MaximumActions) throw std::length_error("Input map exceeds its 256-action limit.");
            if (FindAction(action.Name) != nullptr) throw std::invalid_argument("Input action names must be unique.");
            m_Actions.push_back(std::move(action));
        }

        void AddBinding(InputBinding binding)
        {
            const auto* action = FindAction(binding.Action);
            if (action == nullptr) throw std::invalid_argument("Input binding references an unknown action: " + binding.Action);
            if (m_Bindings.size() >= MaximumBindings) throw std::length_error("Input map exceeds its 2048-binding limit.");
            ValidateBinding(binding, action->Type);
            m_Bindings.push_back(std::move(binding));
        }

        [[nodiscard]] const std::vector<InputActionDefinition>& Actions() const noexcept { return m_Actions; }
        [[nodiscard]] const std::vector<InputBinding>& Bindings() const noexcept { return m_Bindings; }
        [[nodiscard]] const InputActionDefinition* FindAction(std::string_view name) const noexcept
        {
            const auto found = std::ranges::find(m_Actions, name, &InputActionDefinition::Name);
            return found == m_Actions.end() ? nullptr : &*found;
        }

        void Validate() const
        {
            std::unordered_set<std::string> bound;
            for (const auto& binding : m_Bindings)
            {
                const auto* action = FindAction(binding.Action);
                if (action == nullptr) throw std::invalid_argument("Input binding references an unknown action.");
                ValidateBinding(binding, action->Type);
                bound.insert(binding.Action);
            }
            for (const auto& action : m_Actions)
                if (!bound.contains(action.Name))
                    throw std::invalid_argument("Input action has no bindings: " + action.Name);
        }

        /// Input: one frame's raw state and standardized gamepad slot.
        /// Output: clamped action value plus down/pressed/released transitions.
        /// Task: make gameplay consume named semantics instead of platform key
        /// codes. Multiple bindings combine additively for axes and as an OR
        /// for button state. Analog deadzones are radially independent per
        /// source axis and rescaled so post-deadzone range remains [0,1].
        [[nodiscard]] InputActionState Evaluate(
            std::string_view actionName, const InputState& input, std::size_t gamepad = 0u) const
        {
            const auto* action = FindAction(actionName);
            if (action == nullptr) throw std::out_of_range("Unknown input action: " + std::string(actionName));
            if (gamepad >= InputState::MaximumGamepads) throw std::out_of_range("Input action gamepad slot is unsupported.");
            InputVector2 current{};
            InputVector2 previous{};
            bool currentDigital = false;
            bool previousDigital = false;
            for (const auto& binding : m_Bindings)
            {
                if (binding.Action != actionName) continue;
                const auto sample = Sample(binding, input, gamepad, false);
                const auto prior = Sample(binding, input, gamepad, true);
                current.X += sample * binding.Scale.X;
                current.Y += sample * binding.Scale.Y;
                previous.X += prior * binding.Scale.X;
                previous.Y += prior * binding.Scale.Y;
                currentDigital = currentDigital || std::abs(sample) > 1.0e-5f;
                previousDigital = previousDigital || std::abs(prior) > 1.0e-5f;
            }
            current.X = std::clamp(current.X, -1.0f, 1.0f);
            current.Y = std::clamp(current.Y, -1.0f, 1.0f);
            previous.X = std::clamp(previous.X, -1.0f, 1.0f);
            previous.Y = std::clamp(previous.Y, -1.0f, 1.0f);
            if (action->Type != InputActionValueType::Axis2D)
            {
                current.Y = 0.0f;
                previous.Y = 0.0f;
            }
            const auto active = [](const InputVector2& value) {
                return std::abs(value.X) > 1.0e-5f || std::abs(value.Y) > 1.0e-5f;
            };
            const bool down = action->Type == InputActionValueType::Button ? currentDigital : active(current);
            const bool wasDown = action->Type == InputActionValueType::Button ? previousDigital : active(previous);
            return { current, down, down && !wasDown, !down && wasDown };
        }

        [[nodiscard]] static std::string_view ControlCodeName(
            InputBindingDevice device, std::int32_t code);

        friend bool operator==(const InputActionMap&, const InputActionMap&) = default;

    private:
        std::vector<InputActionDefinition> m_Actions;
        std::vector<InputBinding> m_Bindings;

        static void ValidateBinding(const InputBinding& binding, InputActionValueType type)
        {
            if (!std::isfinite(binding.Scale.X) || !std::isfinite(binding.Scale.Y) ||
                (binding.Scale.X == 0.0f && binding.Scale.Y == 0.0f))
                throw std::invalid_argument("Input binding scale must be finite and non-zero.");
            if (type != InputActionValueType::Axis2D && binding.Scale.Y != 0.0f)
                throw std::invalid_argument("Button and 1D input bindings cannot contribute a Y value.");
            if (!std::isfinite(binding.DeadZone) || binding.DeadZone < 0.0f || binding.DeadZone >= 1.0f)
                throw std::invalid_argument("Input binding deadzone must be in [0, 1).");
            if (binding.Device != InputBindingDevice::GamepadAxis && binding.DeadZone != 0.0f)
                throw std::invalid_argument("Only gamepad-axis bindings may define a deadzone.");
            (void)ControlCodeName(binding.Device, binding.Code);
        }

        [[nodiscard]] static float Sample(const InputBinding& binding,
            const InputState& input, std::size_t gamepad, bool previous)
        {
            bool digital = false;
            switch (binding.Device)
            {
                case InputBindingDevice::Keyboard:
                    digital = previous ? input.WasKeyDown(binding.Code)
                        : input.IsKeyDown(binding.Code);
                    break;
                case InputBindingDevice::MouseButton:
                    digital = previous ? input.WasMouseButtonDown(binding.Code)
                        : input.IsMouseButtonDown(binding.Code);
                    break;
                case InputBindingDevice::GamepadButton:
                    digital = previous ? input.WasGamepadButtonDown(gamepad, binding.Code)
                        : input.IsGamepadButtonDown(gamepad, binding.Code);
                    break;
                case InputBindingDevice::GamepadAxis:
                {
                    float value = previous ? input.PreviousGamepadAxisValue(gamepad, binding.Code)
                        : input.GamepadAxisValue(gamepad, binding.Code);
                    const float magnitude = std::abs(value);
                    if (magnitude <= binding.DeadZone) return 0.0f;
                    return std::copysign((magnitude - binding.DeadZone) / (1.0f - binding.DeadZone), value);
                }
            }
            return digital ? 1.0f : 0.0f;
        }

    };

    namespace input_map_detail
    {
        constexpr std::size_t MaximumInputMapBytes = 4u * 1024u * 1024u;

        [[nodiscard]] inline std::string_view TypeName(InputActionValueType type)
        {
            switch (type) { case InputActionValueType::Button: return "button";
                case InputActionValueType::Axis1D: return "axis1d"; case InputActionValueType::Axis2D: return "axis2d"; }
            throw std::invalid_argument("Unknown input action value type.");
        }
        [[nodiscard]] inline InputActionValueType ParseType(std::string_view value)
        {
            if (value == "button") return InputActionValueType::Button;
            if (value == "axis1d") return InputActionValueType::Axis1D;
            if (value == "axis2d") return InputActionValueType::Axis2D;
            throw std::invalid_argument("unknown input action value type");
        }
        [[nodiscard]] inline std::string_view DeviceName(InputBindingDevice device)
        {
            switch (device) { case InputBindingDevice::Keyboard: return "key";
                case InputBindingDevice::MouseButton: return "mouse-button";
                case InputBindingDevice::GamepadButton: return "gamepad-button";
                case InputBindingDevice::GamepadAxis: return "gamepad-axis"; }
            throw std::invalid_argument("Unknown input binding device.");
        }
        [[nodiscard]] inline InputBindingDevice ParseDevice(std::string_view value)
        {
            if (value == "key") return InputBindingDevice::Keyboard;
            if (value == "mouse-button") return InputBindingDevice::MouseButton;
            if (value == "gamepad-button") return InputBindingDevice::GamepadButton;
            if (value == "gamepad-axis") return InputBindingDevice::GamepadAxis;
            throw std::invalid_argument("unknown input binding device");
        }
        [[nodiscard]] inline float ParseFloat(const FormatToken& token)
        {
            float value = 0.0f;
            const auto [end, error] = std::from_chars(token.Text.data(),
                token.Text.data() + token.Text.size(), value, std::chars_format::general);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size() || !std::isfinite(value))
                throw std::invalid_argument("expected a finite decimal number");
            return value;
        }

        template<class Function>
        decltype(auto) Located(std::size_t line, const FormatToken& token, Function&& function)
        {
            try { return std::forward<Function>(function)(); }
            catch (const InputMapFormatError&) { throw; }
            catch (const std::exception& error)
            {
                throw InputMapFormatError(line, token.Column, error.what());
            }
        }

        [[nodiscard]] inline std::int32_t ParseControlCode(InputBindingDevice device, std::string_view name)
        {
            if (device == InputBindingDevice::Keyboard)
            {
                if (name.size() == 1u && name[0] >= 'A' && name[0] <= 'Z') return name[0];
                if (name.size() == 1u && name[0] >= '0' && name[0] <= '9') return name[0];
#define KAIRO_KEY(Name) if (name == #Name) return static_cast<std::int32_t>(KeyCode::Name)
                KAIRO_KEY(Space); KAIRO_KEY(Enter); KAIRO_KEY(Tab); KAIRO_KEY(Backspace);
                KAIRO_KEY(Escape); KAIRO_KEY(Insert); KAIRO_KEY(Delete); KAIRO_KEY(Right);
                KAIRO_KEY(Left); KAIRO_KEY(Down); KAIRO_KEY(Up); KAIRO_KEY(PageUp);
                KAIRO_KEY(PageDown); KAIRO_KEY(Home); KAIRO_KEY(End); KAIRO_KEY(LeftShift);
                KAIRO_KEY(LeftControl); KAIRO_KEY(LeftAlt); KAIRO_KEY(LeftSuper);
                KAIRO_KEY(RightShift); KAIRO_KEY(RightControl); KAIRO_KEY(RightAlt);
                KAIRO_KEY(RightSuper); KAIRO_KEY(F1); KAIRO_KEY(F2); KAIRO_KEY(F3);
                KAIRO_KEY(F4); KAIRO_KEY(F5); KAIRO_KEY(F6); KAIRO_KEY(F7); KAIRO_KEY(F8);
                KAIRO_KEY(F9); KAIRO_KEY(F10); KAIRO_KEY(F11); KAIRO_KEY(F12);
#undef KAIRO_KEY
            }
            else if (device == InputBindingDevice::MouseButton)
            {
                if (name == "Left") return 0; if (name == "Right") return 1; if (name == "Middle") return 2;
            }
            else if (device == InputBindingDevice::GamepadButton)
            {
                constexpr std::string_view names[] = { "A", "B", "X", "Y", "LeftBumper", "RightBumper",
                    "Back", "Start", "Guide", "LeftThumb", "RightThumb", "DpadUp", "DpadRight", "DpadDown", "DpadLeft" };
                for (std::int32_t index = 0; index < static_cast<std::int32_t>(std::size(names)); ++index)
                    if (names[index] == name) return index;
            }
            else
            {
                constexpr std::string_view names[] = { "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };
                for (std::int32_t index = 0; index < static_cast<std::int32_t>(std::size(names)); ++index)
                    if (names[index] == name) return index;
            }
            throw std::invalid_argument("unknown control name for selected input device");
        }
    }

    inline std::string_view InputActionMap::ControlCodeName(InputBindingDevice device, std::int32_t code)
    {
        if (device == InputBindingDevice::Keyboard)
        {
            static constexpr std::string_view letters[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z" };
            static constexpr std::string_view digits[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
            if (code >= 'A' && code <= 'Z') return letters[code - 'A'];
            if (code >= '0' && code <= '9') return digits[code - '0'];
#define KAIRO_KEY_CASE(Name) case static_cast<std::int32_t>(KeyCode::Name): return #Name
            switch (code) { KAIRO_KEY_CASE(Space); KAIRO_KEY_CASE(Enter); KAIRO_KEY_CASE(Tab);
                KAIRO_KEY_CASE(Backspace); KAIRO_KEY_CASE(Escape); KAIRO_KEY_CASE(Insert);
                KAIRO_KEY_CASE(Delete); KAIRO_KEY_CASE(Right); KAIRO_KEY_CASE(Left);
                KAIRO_KEY_CASE(Down); KAIRO_KEY_CASE(Up); KAIRO_KEY_CASE(PageUp);
                KAIRO_KEY_CASE(PageDown); KAIRO_KEY_CASE(Home); KAIRO_KEY_CASE(End);
                KAIRO_KEY_CASE(LeftShift); KAIRO_KEY_CASE(LeftControl); KAIRO_KEY_CASE(LeftAlt);
                KAIRO_KEY_CASE(LeftSuper); KAIRO_KEY_CASE(RightShift); KAIRO_KEY_CASE(RightControl);
                KAIRO_KEY_CASE(RightAlt); KAIRO_KEY_CASE(RightSuper); KAIRO_KEY_CASE(F1);
                KAIRO_KEY_CASE(F2); KAIRO_KEY_CASE(F3); KAIRO_KEY_CASE(F4); KAIRO_KEY_CASE(F5);
                KAIRO_KEY_CASE(F6); KAIRO_KEY_CASE(F7); KAIRO_KEY_CASE(F8); KAIRO_KEY_CASE(F9);
                KAIRO_KEY_CASE(F10); KAIRO_KEY_CASE(F11); KAIRO_KEY_CASE(F12); default: break; }
#undef KAIRO_KEY_CASE
        }
        else if (device == InputBindingDevice::MouseButton)
        {
            constexpr std::string_view names[] = { "Left", "Right", "Middle" };
            if (code >= 0 && code < static_cast<std::int32_t>(std::size(names))) return names[code];
        }
        else if (device == InputBindingDevice::GamepadButton)
        {
            constexpr std::string_view names[] = { "A", "B", "X", "Y", "LeftBumper", "RightBumper",
                "Back", "Start", "Guide", "LeftThumb", "RightThumb", "DpadUp", "DpadRight", "DpadDown", "DpadLeft" };
            if (code >= 0 && code < static_cast<std::int32_t>(std::size(names))) return names[code];
        }
        else
        {
            constexpr std::string_view names[] = { "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };
            if (code >= 0 && code < static_cast<std::int32_t>(std::size(names))) return names[code];
        }
        throw std::invalid_argument("Input binding control code is unsupported for its device.");
    }

    [[nodiscard]] inline InputActionMap ParseInputActionMap(std::string_view source)
    {
        using namespace input_map_detail;
        if (source.size() > MaximumInputMapBytes) throw std::length_error("Kairo input map exceeds the 4 MiB safety limit.");
        InputActionMap result;
        std::istringstream input{ std::string(source) };
        std::string lineText;
        std::size_t line = 0u;
        bool header = false;
        while (std::getline(input, lineText))
        {
            ++line;
            const auto tokens = TokenizeFormatLine<InputMapFormatError>(lineText, line);
            if (tokens.empty()) continue;
            if (!header)
            {
                RequireTokenCount<InputMapFormatError>(tokens, 2u, line, "kairo-input header");
                if (tokens[0].Text != "kairo-input") throw InputMapFormatError(line, tokens[0].Column, "expected kairo-input header");
                if (tokens[1].Text != "1") throw InputMapFormatError(line, tokens[1].Column, "unsupported input map version");
                header = true;
                continue;
            }
            if (tokens[0].Text == "action")
            {
                RequireTokenCount<InputMapFormatError>(tokens, 3u, line, "action");
                const auto type = Located(line, tokens[2], [&] { return ParseType(tokens[2].Text); });
                Located(line, tokens[1], [&] { result.AddAction({ tokens[1].Text, type }); });
            }
            else if (tokens[0].Text == "bind")
            {
                RequireTokenCount<InputMapFormatError>(tokens, 7u, line, "bind");
                const auto device = Located(line, tokens[2], [&] { return ParseDevice(tokens[2].Text); });
                const auto code = Located(line, tokens[3], [&] { return ParseControlCode(device, tokens[3].Text); });
                const float x = Located(line, tokens[4], [&] { return ParseFloat(tokens[4]); });
                const float y = Located(line, tokens[5], [&] { return ParseFloat(tokens[5]); });
                const float deadZone = Located(line, tokens[6], [&] { return ParseFloat(tokens[6]); });
                Located(line, tokens[1], [&] {
                    result.AddBinding({ tokens[1].Text, device, code, { x, y }, deadZone });
                });
            }
            else throw InputMapFormatError(line, tokens[0].Column, "unknown input map statement");
        }
        if (!header) throw InputMapFormatError(1u, 1u, "missing kairo-input header");
        try { result.Validate(); }
        catch (const std::exception& error) { throw InputMapFormatError(line == 0u ? 1u : line, 1u, error.what()); }
        return result;
    }

    [[nodiscard]] inline std::string SerializeInputActionMap(const InputActionMap& map)
    {
        using namespace input_map_detail;
        map.Validate();
        std::ostringstream output;
        output << "kairo-input 1\n";
        for (const auto& action : map.Actions())
            output << "action " << QuoteFormatText(action.Name) << ' ' << TypeName(action.Type) << '\n';
        for (const auto& binding : map.Bindings())
            output << "bind " << QuoteFormatText(binding.Action) << ' ' << DeviceName(binding.Device) << ' '
                << InputActionMap::ControlCodeName(binding.Device, binding.Code) << ' '
                << binding.Scale.X << ' ' << binding.Scale.Y << ' ' << binding.DeadZone << '\n';
        return output.str();
    }

    [[nodiscard]] inline InputActionMap LoadInputActionMap(const std::filesystem::path& path)
    {
        return ParseInputActionMap(LoadBoundedTextFile(path,
            input_map_detail::MaximumInputMapBytes, "Kairo input map"));
    }

    inline void SaveInputActionMap(const std::filesystem::path& path, const InputActionMap& map)
    {
        SaveTextFileAtomically(path, SerializeInputActionMap(map), "Kairo input map");
    }
}
