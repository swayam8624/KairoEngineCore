module;
#include <string>
export module Kairo.EngineCore.Layer;
import Kairo.EngineCore.Event;
export namespace kairo::engine
{
    /// Lifecycle extension point. Layers do not own renderer/physics globals;
    /// a host composition root provides those dependencies explicitly.
    class Layer
    {
    public:
        explicit Layer(std::string name) : m_Name(std::move(name)) {}
        virtual ~Layer() = default;
        [[nodiscard]] const std::string& Name() const noexcept { return m_Name; }
        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float) {}
        virtual void OnEvent(Event&) {}
    private:
        std::string m_Name;
    };
}
