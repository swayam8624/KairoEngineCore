module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.RuntimeWorld;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Scene;
import Kairo.Foundation.Math;

export namespace kairo::engine
{
    struct RuntimeSpawnTicket final
    {
        std::uint64_t Value = 0u;
        [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0u; }
        friend constexpr bool operator==(RuntimeSpawnTicket, RuntimeSpawnTicket) noexcept = default;
    };

    struct RuntimeSpawnRequest final
    {
        std::string Name = "Entity";
        kairo::foundation::math::Transformf Local{};
        bool Enabled = true;
        std::uint32_t Layer = 0u;
        std::vector<std::string> Tags;
        std::optional<Entity> Parent;
    };

    struct RuntimeCommitResult final
    {
        std::uint64_t Revision = 0u;
        std::vector<std::pair<RuntimeSpawnTicket, Entity>> Spawned;
        std::vector<Entity> Destroyed;

        [[nodiscard]] std::optional<Entity> Resolve(RuntimeSpawnTicket ticket) const
        {
            const auto found = std::ranges::find(Spawned, ticket,
                [](const auto& entry) { return entry.first; });
            return found == Spawned.end() ? std::nullopt : std::optional<Entity>{ found->second };
        }
    };

    /// Transactional runtime facade over the authored Scene representation.
    /// Structural mutations are queued and applied to a copy. A failing command
    /// leaves both the live scene and its revision unchanged, which prevents a
    /// half-spawned gameplay frame from leaking into rendering or physics.
    class RuntimeWorld final
    {
    public:
        RuntimeWorld() = default;
        explicit RuntimeWorld(Scene scene) : m_Scene(std::move(scene)) {}

        [[nodiscard]] const Scene& Snapshot() const noexcept { return m_Scene; }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }
        [[nodiscard]] std::size_t PendingCommandCount() const noexcept { return m_Commands.size(); }

        [[nodiscard]] std::optional<Entity> FindFirstByName(std::string_view name) const
        {
            for (const Entity entity : m_Scene.Entities())
                if (m_Scene.Name(entity).Value == name) return entity;
            return std::nullopt;
        }

        [[nodiscard]] std::vector<Entity> FindByTag(std::string_view tag) const
        {
            std::vector<Entity> result;
            for (const Entity entity : m_Scene.Entities())
                if (m_Scene.HasTag(entity, tag)) result.push_back(entity);
            return result;
        }

        [[nodiscard]] RuntimeSpawnTicket QueueSpawn(RuntimeSpawnRequest request)
        {
            if (m_NextTicket == 0u)
                throw std::overflow_error("Runtime spawn ticket space is exhausted.");
            const RuntimeSpawnTicket ticket{ m_NextTicket++ };
            m_Commands.emplace_back(SpawnCommand{ ticket, std::move(request) });
            return ticket;
        }

        void QueueDestroy(Entity entity) { m_Commands.emplace_back(DestroyCommand{ entity }); }
        void QueueRename(Entity entity, std::string name)
        {
            m_Commands.emplace_back(RenameCommand{ entity, std::move(name) });
        }
        void QueueSetTransform(Entity entity, kairo::foundation::math::Transformf local)
        {
            m_Commands.emplace_back(TransformCommand{ entity, std::move(local) });
        }
        void QueueSetEnabled(Entity entity, bool enabled)
        {
            m_Commands.emplace_back(EnabledCommand{ entity, enabled });
        }
        void QueueSetParent(Entity entity, std::optional<Entity> parent)
        {
            m_Commands.emplace_back(ParentCommand{ entity, parent });
        }
        void QueueAddTag(Entity entity, std::string tag)
        {
            m_Commands.emplace_back(AddTagCommand{ entity, std::move(tag) });
        }
        void QueueRemoveTag(Entity entity, std::string tag)
        {
            m_Commands.emplace_back(RemoveTagCommand{ entity, std::move(tag) });
        }

        void DiscardPending() noexcept { m_Commands.clear(); }

        [[nodiscard]] RuntimeCommitResult Commit()
        {
            if (m_Commands.empty()) return { m_Revision, {}, {} };
            if (m_Revision == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Runtime world revision is exhausted.");

            Scene candidate = m_Scene;
            RuntimeCommitResult result;
            for (const Command& command : m_Commands)
            {
                std::visit([&](const auto& typed) { Apply(candidate, typed, result); }, command);
            }
            m_Scene = std::move(candidate);
            m_Commands.clear();
            result.Revision = ++m_Revision;
            return result;
        }

        void Replace(Scene scene)
        {
            if (m_Revision == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Runtime world revision is exhausted.");
            m_Scene = std::move(scene);
            m_Commands.clear();
            ++m_Revision;
        }

    private:
        struct SpawnCommand final { RuntimeSpawnTicket Ticket; RuntimeSpawnRequest Request; };
        struct DestroyCommand final { Entity Target; };
        struct RenameCommand final { Entity Target; std::string Name; };
        struct TransformCommand final { Entity Target; kairo::foundation::math::Transformf Local; };
        struct EnabledCommand final { Entity Target; bool Enabled; };
        struct ParentCommand final { Entity Target; std::optional<Entity> Parent; };
        struct AddTagCommand final { Entity Target; std::string Tag; };
        struct RemoveTagCommand final { Entity Target; std::string Tag; };
        using Command = std::variant<SpawnCommand, DestroyCommand, RenameCommand,
            TransformCommand, EnabledCommand, ParentCommand, AddTagCommand, RemoveTagCommand>;

        Scene m_Scene;
        std::vector<Command> m_Commands;
        std::uint64_t m_Revision = 0u;
        std::uint64_t m_NextTicket = 1u;

        static void Apply(Scene& scene, const SpawnCommand& command, RuntimeCommitResult& result)
        {
            Entity entity = scene.CreateEntity(command.Request.Name);
            scene.Transform(entity).Local = command.Request.Local;
            scene.SetEnabled(entity, command.Request.Enabled);
            scene.SetLayer(entity, command.Request.Layer);
            for (const std::string& tag : command.Request.Tags) scene.AddTag(entity, tag);
            if (command.Request.Parent.has_value())
                scene.SetParent(entity, command.Request.Parent);
            result.Spawned.emplace_back(command.Ticket, entity);
        }

        static void Apply(Scene& scene, const DestroyCommand& command, RuntimeCommitResult& result)
        {
            if (!scene.Contains(command.Target))
                throw std::out_of_range("Runtime destroy targets an unknown entity.");
            scene.DestroyEntity(command.Target);
            result.Destroyed.push_back(command.Target);
        }

        static void Apply(Scene& scene, const RenameCommand& command, RuntimeCommitResult&)
        {
            if (command.Name.empty())
                throw std::invalid_argument("Runtime entity name cannot be empty.");
            scene.Name(command.Target).Value = command.Name;
        }

        static void Apply(Scene& scene, const TransformCommand& command, RuntimeCommitResult&)
        {
            scene.Transform(command.Target).Local = command.Local;
        }

        static void Apply(Scene& scene, const EnabledCommand& command, RuntimeCommitResult&)
        {
            scene.SetEnabled(command.Target, command.Enabled);
        }

        static void Apply(Scene& scene, const ParentCommand& command, RuntimeCommitResult&)
        {
            scene.SetParent(command.Target, command.Parent);
        }

        static void Apply(Scene& scene, const AddTagCommand& command, RuntimeCommitResult&)
        {
            scene.AddTag(command.Target, command.Tag);
        }

        static void Apply(Scene& scene, const RemoveTagCommand& command, RuntimeCommitResult&)
        {
            (void)scene.RemoveTag(command.Target, command.Tag);
        }
    };
}
