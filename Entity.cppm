module;
#include <compare>
#include <cstdint>
export module Kairo.EngineCore.Entity;
export namespace kairo::engine
{
    /// Stable scene-local entity identity. Zero is reserved as the invalid ID.
    struct Entity final
    {
        std::uint32_t Value = 0u;
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return Value != 0u; }
        // Keep the comparison category explicit across C++ module boundaries.
        // Equality and ordering remain exactly the unsigned identity order.
        [[nodiscard]] constexpr bool operator==(const Entity&) const noexcept = default;
        [[nodiscard]] constexpr std::strong_ordering operator<=>(const Entity& other) const noexcept
        {
            return Value <=> other.Value;
        }
    };
}
