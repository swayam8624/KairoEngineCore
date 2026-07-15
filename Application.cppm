module;
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
export module Kairo.EngineCore.Application;
import Kairo.EngineCore.Scene;
import Kairo.EngineCore.Time;
import Kairo.EngineCore.Event;
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
        }
        void Dispatch(Event& event)
        {
            for (auto it = m_Layers.rbegin(); it != m_Layers.rend() && !event.Handled; ++it) (*it)->OnEvent(event);
        }
        ~Application() { for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it) (*it)->OnDetach(); }
    private:
        Scene m_Scene;
        FrameClock m_Clock;
        std::vector<std::unique_ptr<Layer>> m_Layers;
    };
}
