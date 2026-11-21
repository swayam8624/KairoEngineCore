// SOURCE STUDY NOTE:
// Platform-neutral application/layer orchestration. External host feeds Event and InputState,
// Application drives Scene/Layer lifecycle, and renderer/physics systems are injected through layers
// rather than imported directly.
// Related modules and exercises: docs/SOURCE_READING_ORDER.md.

module;
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
export module Kairo.EngineCore.Application;
import Kairo.EngineCore.Scene;
import Kairo.EngineCore.Time;
import Kairo.EngineCore.Event;
import Kairo.EngineCore.Input;
import Kairo.EngineCore.Layer;
export namespace kairo::engine
{
    /// Application owns scene/layer lifetime while platform hosts feed events
    /// and call RunFrame. Renderer and physics are injected as concrete layers,
    /// keeping EngineCore testable and platform neutral.
    class Application final
    {
    public:
        [[nodiscard]] Scene& ActiveScene() noexcept { return m_Scene; }
        [[nodiscard]] const FrameClock& Clock() const noexcept { return m_Clock; }
        [[nodiscard]] InputState& Input() noexcept { return m_Input; }
        [[nodiscard]] const InputState& Input() const noexcept { return m_Input; }
        /// Host composition: take ownership of a Layer and attach it to the
        /// Application. Rendering/physics integrations enter through this
        /// interface, not as hidden EngineCore global objects.
        void PushLayer(std::unique_ptr<Layer> layer)
        {
            if (!layer) throw std::invalid_argument("Application cannot own a null layer.");
            layer->OnAttach();
            m_Layers.push_back(std::move(layer));
        }
        /// Frame boundary: tick the FrameClock once, update layers in their
        /// insertion order with the measured delta, then CommitFrame on
        /// InputState so edge transitions are ready for the next frame.
        /// This is *not* the fixed-step PhysicsWorld integrator.
        void RunFrame()
        {
            m_Clock.Tick();
            for (const auto& layer : m_Layers) layer->OnUpdate(m_Clock.DeltaSeconds());
            m_Input.CommitFrame();
        }
        /// Host event path: first update raw InputState, then dispatch the
        /// mutable event to layers from last added to first until Handled.
        /// An overlay can intercept UI actions without preventing raw input
        /// state collection; application code chooses how to route actions.
        void Dispatch(Event& event)
        {
            m_Input.Consume(event);
            for (auto it = m_Layers.rbegin(); it != m_Layers.rend() && !event.Handled; ++it) (*it)->OnEvent(event);
        }
        /// Detach layers in reverse order so dependent overlays and tools
        /// release their resources before the layers beneath them.
        ~Application() { for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it) (*it)->OnDetach(); }
    private:
        Scene m_Scene;
        FrameClock m_Clock;
        InputState m_Input;
        std::vector<std::unique_ptr<Layer>> m_Layers;
    };
}
