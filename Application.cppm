export module Kairo.EngineCore.Application;
import Kairo.EngineCore.Scene;
import Kairo.EngineCore.Time;
export namespace kairo::engine
{
    /// Non-owning host loop base. Renderer/physics integration is injected by
    /// a later executable layer, keeping core testable and platform neutral.
    class Application final
    {
    public:
        [[nodiscard]] Scene& ActiveScene() noexcept { return m_Scene; }
        [[nodiscard]] const FrameClock& Clock() const noexcept { return m_Clock; }
        void BeginFrame() noexcept { m_Clock.Tick(); }
    private:
        Scene m_Scene;
        FrameClock m_Clock;
    };
}
