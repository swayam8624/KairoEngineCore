module;
#include <cstdint>
#include <string_view>
export module Kairo.EngineCore.Event;
export namespace kairo::engine
{
    enum class EventType : std::uint8_t
    {
        None, WindowClose, WindowResize, KeyPressed, KeyReleased,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };
    /// Renderer/window independent event envelope. Platform adapters populate
    /// values; layers may mark it handled to stop regular propagation.
    struct Event final
    {
        EventType Type = EventType::None;
        bool Handled = false;
        std::int32_t A = 0;
        std::int32_t B = 0;
        [[nodiscard]] constexpr std::string_view Name() const noexcept
        {
            switch (Type) { case EventType::WindowClose: return "WindowClose"; case EventType::WindowResize: return "WindowResize"; case EventType::KeyPressed: return "KeyPressed"; case EventType::KeyReleased: return "KeyReleased"; case EventType::MouseButtonPressed: return "MouseButtonPressed"; case EventType::MouseButtonReleased: return "MouseButtonReleased"; case EventType::MouseMoved: return "MouseMoved"; case EventType::MouseScrolled: return "MouseScrolled"; default: return "None"; }
        }
    };
}
