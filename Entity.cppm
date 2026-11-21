// TEACHING MAP — Entity.cppm
// Stable entity identity used by Scene, serialization, runtime scene commands and editor
// selection. Differentiate persistent scene IDs from process-local KairoECS generations and
// physics body handles.
// See READING_ORDER.md and sibling Player/Renderer/Physics interfaces.

module;
#include <compare>
#include <cstdint>
export module Kairo.EngineCore.Entity;
export namespace kairo::engine
{
    /// Stable scene-local entity identity. Zero is reserved as the invalid ID.
    struct Entity final { std::uint32_t Value = 0u; [[nodiscard]] constexpr explicit operator bool() const noexcept { return Value != 0u; } auto operator<=>(const Entity&) const = default; };
}
