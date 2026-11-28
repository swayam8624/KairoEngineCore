#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

import Kairo.EngineCore.Entity;

using kairo::engine::Entity;
static_assert(std::is_same_v<decltype(Entity{} <=> Entity{}), std::strong_ordering>);
static_assert(std::is_trivially_copyable_v<Entity>);
static_assert(sizeof(Entity) == sizeof(std::uint32_t));
static_assert(!static_cast<bool>(Entity{}));
static_assert(Entity{1u} < Entity{2u});
static_assert(Entity{2u} > Entity{1u});
static_assert(Entity{1u} == Entity{1u});
static_assert(Entity{1u} != Entity{2u});
static_assert(Entity{std::numeric_limits<std::uint32_t>::max()} > Entity{1u});

int main()
{
    std::optional<Entity> selected;
    if (selected) return 1;
    selected = Entity{42u};
    const auto copied = selected;
    if (copied != selected || copied != Entity{42u}) return 2;
    selected = Entity{43u};
    if (!(copied < selected)) return 3;
    selected.reset();
    if (selected || !copied) return 4;
    return 0;
}
