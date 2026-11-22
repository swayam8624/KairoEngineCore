// TEACHING MODULE: Platform-neutral frame orchestration.
// The native Player/Editor host pumps OS events and owns window/renderer
// lifetime, while Application owns a Scene, input snapshots and Layer
// objects. RunFrame advances the clock and calls injected layer callbacks.
// A Layer may bridge to PhysicsEngine/Renderer; Application itself should
// not import their native device or solver APIs.
//
// Events travel through a common typed interface, not raw GLFW callbacks.
// Keep frame ordering (input snapshots, updates and event dispatch)
// consistent when implementing new platform hosts.
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
    // Application owns layers through unique_ptr. OnAttach is called
    // when a layer is pushed; the host supplies native services separately.
    class Application final
    {
    public:
        [[nodiscard]] Scene& ActiveScene() noexcept { return m_Scene; }
        [[nodiscard]] const FrameClock& Clock() const noexcept { return m_Clock; }
        [[nodiscard]] InputState& Input() noexcept { return m_Input; }
        [[nodiscard]] const InputState& Input() const noexcept { return m_Input; }
        void PushLayer(std::unique_ptr<Layer> layer)
        {
            if (!layer) throw std::invalid_argument("Application cannot own a null layer.");
            layer->OnAttach();
            m_Layers.push_back(std::move(layer));
        }
        void RunFrame()
        {
            m_Clock.Tick();
            for (const auto& layer : m_Layers) layer->OnUpdate(m_Clock.DeltaSeconds());
            m_Input.CommitFrame();
        }
        void Dispatch(Event& event)
        {
            m_Input.Consume(event);
            for (auto it = m_Layers.rbegin(); it != m_Layers.rend() && !event.Handled; ++it) (*it)->OnEvent(event);
        }
        ~Application() { for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it) (*it)->OnDetach(); }
    private:
        Scene m_Scene;
        FrameClock m_Clock;
        InputState m_Input;
        std::vector<std::unique_ptr<Layer>> m_Layers;
    };
}
