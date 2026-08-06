module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.RuntimeSceneCommands;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Scene;
import Kairo.Foundation.Math;

export namespace kairo::engine
{
    struct PendingEntity final
    {
        std::uint32_t Value = 0u;
        friend constexpr bool operator==(PendingEntity, PendingEntity) noexcept = default;
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return Value != 0u; }
    };

    using RuntimeEntityReference = std::variant<Entity, PendingEntity>;

    struct RuntimeCreateEntity final
    {
        PendingEntity Result;
        std::string Name = "Entity";
        std::optional<RuntimeEntityReference> Parent;
    };

    struct RuntimeDestroyEntity final { RuntimeEntityReference Target; };
    struct RuntimeSetEnabled final { RuntimeEntityReference Target; bool Enabled = true; };
    struct RuntimeSetPosition final
    {
        RuntimeEntityReference Target;
        kairo::foundation::math::Vec3f Position{};
    };
    struct RuntimeSetParent final
    {
        RuntimeEntityReference Child;
        std::optional<RuntimeEntityReference> Parent;
    };
    struct RuntimeAddTag final { RuntimeEntityReference Target; std::string Tag; };
    struct RuntimeRemoveTag final { RuntimeEntityReference Target; std::string Tag; };

    using RuntimeSceneCommand = std::variant<RuntimeCreateEntity, RuntimeDestroyEntity,
        RuntimeSetEnabled, RuntimeSetPosition, RuntimeSetParent, RuntimeAddTag, RuntimeRemoveTag>;

    struct RuntimeSceneCommit final
    {
        std::unordered_map<std::uint32_t, Entity> Created;
        std::size_t AppliedCommands = 0u;

        [[nodiscard]] Entity Resolve(PendingEntity pending) const
        {
            const auto found = Created.find(pending.Value);
            if (found == Created.end()) throw std::out_of_range("Pending entity was not created by this commit.");
            return found->second;
        }
    };

    /// FIFO structural mutation queue for gameplay code. Commands are applied
    /// only at explicit synchronization points, so event callbacks and physics
    /// iteration never invalidate the scene traversal currently in progress.
    class RuntimeSceneCommandBuffer final
    {
    public:
        static constexpr std::size_t MaximumCommands = 1'000'000u;

        [[nodiscard]] PendingEntity Create(std::string name = "Entity",
            std::optional<RuntimeEntityReference> parent = std::nullopt)
        {
            if (m_NextPending == 0u) throw std::overflow_error("Runtime command buffer exhausted pending IDs.");
            const PendingEntity pending{ m_NextPending++ };
            Push(RuntimeCreateEntity{ pending, std::move(name), std::move(parent) });
            return pending;
        }

        void Destroy(RuntimeEntityReference target) { Push(RuntimeDestroyEntity{ target }); }
        void SetEnabled(RuntimeEntityReference target, bool enabled) { Push(RuntimeSetEnabled{ target, enabled }); }
        void SetPosition(RuntimeEntityReference target, kairo::foundation::math::Vec3f position)
        {
            Push(RuntimeSetPosition{ target, position });
        }
        void SetParent(RuntimeEntityReference child, std::optional<RuntimeEntityReference> parent)
        {
            Push(RuntimeSetParent{ child, std::move(parent) });
        }
        void AddTag(RuntimeEntityReference target, std::string tag)
        {
            Push(RuntimeAddTag{ target, std::move(tag) });
        }
        void RemoveTag(RuntimeEntityReference target, std::string tag)
        {
            Push(RuntimeRemoveTag{ target, std::move(tag) });
        }

        [[nodiscard]] bool Empty() const noexcept { return m_Commands.empty(); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_Commands.size(); }
        void Clear() noexcept { m_Commands.clear(); }

        [[nodiscard]] RuntimeSceneCommit Commit(Scene& scene)
        {
            RuntimeSceneCommit result;
            result.Created.reserve(m_Commands.size());
            const auto resolve = [&result, &scene](const RuntimeEntityReference& reference) -> Entity {
                if (const auto* entity = std::get_if<Entity>(&reference))
                {
                    if (!scene.Contains(*entity)) throw std::out_of_range("Runtime command references a missing entity.");
                    return *entity;
                }
                const PendingEntity pending = std::get<PendingEntity>(reference);
                if (!pending) throw std::invalid_argument("Runtime command references an invalid pending entity.");
                return result.Resolve(pending);
            };

            for (const RuntimeSceneCommand& command : m_Commands)
            {
                std::visit([&](const auto& value) {
                    using Command = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Command, RuntimeCreateEntity>)
                    {
                        if (!value.Result || result.Created.contains(value.Result.Value))
                            throw std::invalid_argument("Runtime create command has a duplicate pending entity.");
                        const Entity entity = scene.CreateEntity(value.Name);
                        result.Created.emplace(value.Result.Value, entity);
                        if (value.Parent.has_value()) scene.SetParent(entity, resolve(*value.Parent));
                    }
                    else if constexpr (std::is_same_v<Command, RuntimeDestroyEntity>)
                    {
                        scene.DestroyEntity(resolve(value.Target));
                    }
                    else if constexpr (std::is_same_v<Command, RuntimeSetEnabled>)
                    {
                        scene.SetEnabled(resolve(value.Target), value.Enabled);
                    }
                    else if constexpr (std::is_same_v<Command, RuntimeSetPosition>)
                    {
                        scene.Transform(resolve(value.Target)).Local.Translation = value.Position;
                    }
                    else if constexpr (std::is_same_v<Command, RuntimeSetParent>)
                    {
                        scene.SetParent(resolve(value.Child), value.Parent.has_value()
                            ? std::optional<Entity>{ resolve(*value.Parent) } : std::nullopt);
                    }
                    else if constexpr (std::is_same_v<Command, RuntimeAddTag>)
                    {
                        scene.AddTag(resolve(value.Target), value.Tag);
                    }
                    else if constexpr (std::is_same_v<Command, RuntimeRemoveTag>)
                    {
                        (void)scene.RemoveTag(resolve(value.Target), value.Tag);
                    }
                }, command);
                ++result.AppliedCommands;
            }
            m_Commands.clear();
            return result;
        }

    private:
        std::uint32_t m_NextPending = 1u;
        std::vector<RuntimeSceneCommand> m_Commands;

        template<class Command>
        void Push(Command command)
        {
            if (m_Commands.size() >= MaximumCommands)
                throw std::length_error("Runtime scene command buffer exceeds its safety limit.");
            m_Commands.emplace_back(std::move(command));
        }
    };

    [[nodiscard]] inline std::vector<Entity> FindEntitiesWithTag(const Scene& scene, std::string_view tag)
    {
        std::vector<Entity> result;
        for (const Entity entity : scene.Entities())
            if (scene.HasTag(entity, tag)) result.push_back(entity);
        return result;
    }

    [[nodiscard]] inline std::optional<Entity> FindFirstEntityWithTag(
        const Scene& scene, std::string_view tag)
    {
        const std::vector<Entity> entities = FindEntitiesWithTag(scene, tag);
        if (entities.empty()) return std::nullopt;
        return entities.front();
    }
}
