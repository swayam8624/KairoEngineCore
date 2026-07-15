module;
#include <chrono>
#include <cstdint>
export module Kairo.EngineCore.Time;
export namespace kairo::engine
{
    /// Monotonic frame clock. Call Tick once per outer application frame.
    class FrameClock final
    {
    public:
        void Tick() noexcept { const auto now = Clock::now(); m_Delta = std::chrono::duration<float>(now - m_Last).count(); m_Last = now; ++m_Frame; }
        [[nodiscard]] float DeltaSeconds() const noexcept { return m_Delta; }
        [[nodiscard]] std::uint64_t FrameIndex() const noexcept { return m_Frame; }
    private:
        using Clock = std::chrono::steady_clock;
        Clock::time_point m_Last = Clock::now();
        float m_Delta = 0.0f;
        std::uint64_t m_Frame = 0u;
    };
}
