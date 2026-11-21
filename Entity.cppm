// SOURCE STUDY NOTE:
// EngineCore scene-local numeric entity identity. Scene.cppm owns creation and lookup; unlike
// KairoECS's generational Entity type it is not automatically a sparse-set component pool handle.
// Related modules and exercises: docs/SOURCE_READING_ORDER.md.

module;
#include <compare>
#include <cstdint>
export module Kairo.EngineCore.Entity;
export namespace kairo::engine
{
    /// Stable scene-local entity identity. Zero is reserved as the invalid ID.
    struct Entity final { std::uint32_t Value = 0u; [[nodiscard]] constexpr explicit operator bool() const noexcept { return Value != 0u; } auto operator<=>(const Entity&) const = default; };
}
