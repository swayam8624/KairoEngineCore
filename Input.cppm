module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>

export module Kairo.EngineCore.Input;

import Kairo.EngineCore.Event;

export namespace kairo::engine
{
    /// Platform-neutral key values intentionally match the common GLFW key
    /// values where defined. A platform adapter may forward any integral key
    /// code through Event::A; the semantic constants cover engine controls.
    enum class KeyCode : std::int32_t
    {
        Unknown = -1, Space = 32, Apostrophe = 39, Comma = 44, Minus = 45,
        Period = 46, Slash = 47, D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260,
        Delete = 261, Right = 262, Left = 263, Down = 264, Up = 265,
        PageUp = 266, PageDown = 267, Home = 268, End = 269,
        CapsLock = 280, ScrollLock = 281, NumLock = 282, PrintScreen = 283, Pause = 284,
        F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
        RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347, Menu = 348
    };

    enum class MouseButton : std::int32_t { Left = 0, Right = 1, Middle = 2 };
    enum class GamepadButton : std::int32_t
    {
        A = 0, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide,
        LeftThumb, RightThumb, DpadUp, DpadRight, DpadDown, DpadLeft
    };
    enum class GamepadAxis : std::int32_t
    {
        LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger
    };

    struct InputVector2 final
    {
        float X = 0.0f;
        float Y = 0.0f;
        friend constexpr bool operator==(const InputVector2&, const InputVector2&) noexcept = default;
    };

    /// Frame-scoped keyboard/mouse state fed by a platform event adapter.
    /// Input: Event values use A/B as documented by EventType: key code in A,
    /// mouse pixel coordinates for MouseMoved, and scroll delta for MouseScrolled.
    /// Output: stable held-state plus deltas accumulated since BeginFrame.
    /// Task: centralize input semantics independently of GLFW, ImGui, or a
    /// future platform layer. It is intended for a single application thread.
    class InputState final
    {
    public:
        static constexpr std::size_t MaximumGamepads = 4u;
        static constexpr std::size_t MaximumGamepadButtons = 32u;
        static constexpr std::size_t MaximumGamepadAxes = 8u;

        /// Task: establish the prior-state snapshot and clear transient mouse
        /// deltas before a platform adapter collects the next frame's input.
        void BeginFrame() noexcept { CommitFrame(); }

        /// Task: commit current held state after consumers have evaluated the
        /// frame. Application uses this at the end of RunFrame because native
        /// event pumps normally dispatch before layer updates.
        void CommitFrame() noexcept
        {
            m_PreviousKeys = m_Keys;
            m_PreviousMouseButtons = m_MouseButtons;
            m_PreviousGamepadButtons = m_GamepadButtons;
            m_PreviousGamepadAxes = m_GamepadAxes;
            m_MouseDelta = {};
            m_ScrollDelta = {};
        }

        void Consume(const Event& event) noexcept
        {
            switch (event.Type)
            {
            case EventType::KeyPressed: SetKeyDown(event.A, true); break;
            case EventType::KeyReleased: SetKeyDown(event.A, false); break;
            case EventType::MouseButtonPressed: SetMouseButtonDown(event.A, true); break;
            case EventType::MouseButtonReleased: SetMouseButtonDown(event.A, false); break;
            case EventType::MouseMoved:
            {
                SetMousePosition({ static_cast<float>(event.A), static_cast<float>(event.B) });
                break;
            }
            case EventType::MouseScrolled:
                AddScrollDelta({ static_cast<float>(event.A), static_cast<float>(event.B) });
                break;
            default: break;
            }
        }

        void SetKeyDown(std::int32_t key, bool down) noexcept
        {
            if (down) m_Keys.insert(key); else m_Keys.erase(key);
        }
        void SetKeyDown(KeyCode key, bool down) noexcept { SetKeyDown(static_cast<std::int32_t>(key), down); }
        [[nodiscard]] bool IsKeyDown(std::int32_t key) const noexcept { return m_Keys.contains(key); }
        [[nodiscard]] bool WasKeyDown(std::int32_t key) const noexcept { return m_PreviousKeys.contains(key); }
        [[nodiscard]] bool IsKeyDown(KeyCode key) const noexcept { return IsKeyDown(static_cast<std::int32_t>(key)); }
        [[nodiscard]] bool IsKeyPressed(std::int32_t key) const noexcept { return IsKeyDown(key) && !m_PreviousKeys.contains(key); }
        [[nodiscard]] bool IsKeyReleased(std::int32_t key) const noexcept { return !IsKeyDown(key) && m_PreviousKeys.contains(key); }
        [[nodiscard]] bool IsKeyPressed(KeyCode key) const noexcept { return IsKeyPressed(static_cast<std::int32_t>(key)); }
        [[nodiscard]] bool IsKeyReleased(KeyCode key) const noexcept { return IsKeyReleased(static_cast<std::int32_t>(key)); }

        void SetMouseButtonDown(std::int32_t button, bool down) noexcept
        {
            if (down) m_MouseButtons.insert(button); else m_MouseButtons.erase(button);
        }
        [[nodiscard]] bool IsMouseButtonDown(std::int32_t button) const noexcept { return m_MouseButtons.contains(button); }
        [[nodiscard]] bool WasMouseButtonDown(std::int32_t button) const noexcept { return m_PreviousMouseButtons.contains(button); }
        [[nodiscard]] bool IsMouseButtonPressed(std::int32_t button) const noexcept { return IsMouseButtonDown(button) && !m_PreviousMouseButtons.contains(button); }
        [[nodiscard]] bool IsMouseButtonReleased(std::int32_t button) const noexcept { return !IsMouseButtonDown(button) && m_PreviousMouseButtons.contains(button); }
        void SetMouseButtonDown(MouseButton button, bool down) noexcept { SetMouseButtonDown(static_cast<std::int32_t>(button), down); }
        [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const noexcept { return IsMouseButtonDown(static_cast<std::int32_t>(button)); }
        [[nodiscard]] bool IsMouseButtonPressed(MouseButton button) const noexcept { return IsMouseButtonPressed(static_cast<std::int32_t>(button)); }
        [[nodiscard]] bool IsMouseButtonReleased(MouseButton button) const noexcept { return IsMouseButtonReleased(static_cast<std::int32_t>(button)); }

        void SetGamepadButtonDown(std::size_t gamepad, std::size_t button, bool down)
        {
            ValidateGamepadButton(gamepad, button);
            m_GamepadButtons[gamepad][button] = down;
        }
        void SetGamepadButtonDown(std::size_t gamepad, GamepadButton button, bool down)
        {
            SetGamepadButtonDown(gamepad, static_cast<std::size_t>(button), down);
        }
        [[nodiscard]] bool IsGamepadButtonDown(std::size_t gamepad, std::size_t button) const noexcept
        {
            return gamepad < MaximumGamepads && button < MaximumGamepadButtons && m_GamepadButtons[gamepad][button];
        }
        [[nodiscard]] bool WasGamepadButtonDown(std::size_t gamepad, std::size_t button) const noexcept
        {
            return gamepad < MaximumGamepads && button < MaximumGamepadButtons && m_PreviousGamepadButtons[gamepad][button];
        }
        [[nodiscard]] bool IsGamepadButtonPressed(std::size_t gamepad, std::size_t button) const noexcept
        {
            return IsGamepadButtonDown(gamepad, button) && !m_PreviousGamepadButtons[gamepad][button];
        }
        [[nodiscard]] bool IsGamepadButtonReleased(std::size_t gamepad, std::size_t button) const noexcept
        {
            return gamepad < MaximumGamepads && button < MaximumGamepadButtons &&
                !m_GamepadButtons[gamepad][button] && m_PreviousGamepadButtons[gamepad][button];
        }
        [[nodiscard]] bool IsGamepadButtonDown(std::size_t gamepad, GamepadButton button) const noexcept
        {
            return IsGamepadButtonDown(gamepad, static_cast<std::size_t>(button));
        }
        [[nodiscard]] bool IsGamepadButtonPressed(std::size_t gamepad, GamepadButton button) const noexcept
        {
            return IsGamepadButtonPressed(gamepad, static_cast<std::size_t>(button));
        }
        [[nodiscard]] bool IsGamepadButtonReleased(std::size_t gamepad, GamepadButton button) const noexcept
        {
            return IsGamepadButtonReleased(gamepad, static_cast<std::size_t>(button));
        }
        void SetGamepadAxis(std::size_t gamepad, std::size_t axis, float value)
        {
            if (gamepad >= MaximumGamepads || axis >= MaximumGamepadAxes)
                throw std::out_of_range("Gamepad axis index is outside the supported input range.");
            if (!std::isfinite(value)) throw std::invalid_argument("Gamepad axis value must be finite.");
            m_GamepadAxes[gamepad][axis] = std::clamp(value, -1.0f, 1.0f);
        }
        void SetGamepadAxis(std::size_t gamepad, GamepadAxis axis, float value)
        {
            SetGamepadAxis(gamepad, static_cast<std::size_t>(axis), value);
        }
        [[nodiscard]] float GamepadAxisValue(std::size_t gamepad, std::size_t axis) const noexcept
        {
            return gamepad < MaximumGamepads && axis < MaximumGamepadAxes ? m_GamepadAxes[gamepad][axis] : 0.0f;
        }
        [[nodiscard]] float PreviousGamepadAxisValue(std::size_t gamepad, std::size_t axis) const noexcept
        {
            return gamepad < MaximumGamepads && axis < MaximumGamepadAxes ? m_PreviousGamepadAxes[gamepad][axis] : 0.0f;
        }
        [[nodiscard]] float GamepadAxisValue(std::size_t gamepad, GamepadAxis axis) const noexcept
        {
            return GamepadAxisValue(gamepad, static_cast<std::size_t>(axis));
        }
        void ClearGamepad(std::size_t gamepad)
        {
            if (gamepad >= MaximumGamepads) throw std::out_of_range("Gamepad index is outside the supported input range.");
            m_GamepadButtons[gamepad].fill(false);
            m_GamepadAxes[gamepad].fill(0.0f);
        }

        [[nodiscard]] InputVector2 MousePosition() const noexcept { return m_MousePosition; }
        [[nodiscard]] InputVector2 MouseDelta() const noexcept { return m_MouseDelta; }
        [[nodiscard]] InputVector2 ScrollDelta() const noexcept { return m_ScrollDelta; }
        void SetMousePosition(InputVector2 position)
        {
            ValidateVector(position, "Mouse position");
            m_MouseDelta.X += position.X - m_MousePosition.X;
            m_MouseDelta.Y += position.Y - m_MousePosition.Y;
            m_MousePosition = position;
        }
        void AddScrollDelta(InputVector2 delta)
        {
            ValidateVector(delta, "Mouse scroll delta");
            m_ScrollDelta.X += delta.X;
            m_ScrollDelta.Y += delta.Y;
        }

    private:
        std::unordered_set<std::int32_t> m_Keys;
        std::unordered_set<std::int32_t> m_PreviousKeys;
        std::unordered_set<std::int32_t> m_MouseButtons;
        std::unordered_set<std::int32_t> m_PreviousMouseButtons;
        std::array<std::array<bool, MaximumGamepadButtons>, MaximumGamepads> m_GamepadButtons{};
        std::array<std::array<bool, MaximumGamepadButtons>, MaximumGamepads> m_PreviousGamepadButtons{};
        std::array<std::array<float, MaximumGamepadAxes>, MaximumGamepads> m_GamepadAxes{};
        std::array<std::array<float, MaximumGamepadAxes>, MaximumGamepads> m_PreviousGamepadAxes{};
        InputVector2 m_MousePosition{};
        InputVector2 m_MouseDelta{};
        InputVector2 m_ScrollDelta{};

        static void ValidateGamepadButton(std::size_t gamepad, std::size_t button)
        {
            if (gamepad >= MaximumGamepads || button >= MaximumGamepadButtons)
                throw std::out_of_range("Gamepad button index is outside the supported input range.");
        }
        static void ValidateVector(const InputVector2& value, const char* role)
        {
            if (!std::isfinite(value.X) || !std::isfinite(value.Y))
                throw std::invalid_argument(std::string(role) + " must be finite.");
        }
    };
}
