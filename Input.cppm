module;

#include <cstdint>
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
        Delete = 261, Right = 262, Left = 263, Down = 264, Up = 265
    };

    enum class MouseButton : std::int32_t { Left = 0, Right = 1, Middle = 2 };

    struct InputVector2 final { float X = 0.0f; float Y = 0.0f; };

    /// Frame-scoped keyboard/mouse state fed by a platform event adapter.
    /// Input: Event values use A/B as documented by EventType: key code in A,
    /// mouse pixel coordinates for MouseMoved, and scroll delta for MouseScrolled.
    /// Output: stable held-state plus deltas accumulated since BeginFrame.
    /// Task: centralize input semantics independently of GLFW, ImGui, or a
    /// future platform layer. It is intended for a single application thread.
    class InputState final
    {
    public:
        void BeginFrame() noexcept
        {
            m_MouseDelta = {};
            m_ScrollDelta = {};
        }

        void Consume(const Event& event) noexcept
        {
            switch (event.Type)
            {
            case EventType::KeyPressed: m_Keys.insert(event.A); break;
            case EventType::KeyReleased: m_Keys.erase(event.A); break;
            case EventType::MouseMoved:
            {
                const InputVector2 next{ static_cast<float>(event.A), static_cast<float>(event.B) };
                m_MouseDelta = { next.X - m_MousePosition.X, next.Y - m_MousePosition.Y };
                m_MousePosition = next;
                break;
            }
            case EventType::MouseScrolled:
                m_ScrollDelta.X += static_cast<float>(event.A);
                m_ScrollDelta.Y += static_cast<float>(event.B);
                break;
            default: break;
            }
        }

        [[nodiscard]] bool IsKeyDown(KeyCode key) const noexcept { return m_Keys.contains(static_cast<std::int32_t>(key)); }
        [[nodiscard]] InputVector2 MousePosition() const noexcept { return m_MousePosition; }
        [[nodiscard]] InputVector2 MouseDelta() const noexcept { return m_MouseDelta; }
        [[nodiscard]] InputVector2 ScrollDelta() const noexcept { return m_ScrollDelta; }

    private:
        std::unordered_set<std::int32_t> m_Keys;
        InputVector2 m_MousePosition{};
        InputVector2 m_MouseDelta{};
        InputVector2 m_ScrollDelta{};
    };
}
