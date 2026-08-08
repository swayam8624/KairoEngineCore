#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.GameplayVM;
import Kairo.EngineCore.RuntimeWorld;
import Kairo.EngineCore.Scene;
import Kairo.Foundation.Math;
import Kairo.Foundation.Math.Vector;

namespace
{
    class RecordingGameplayHost final : public kairo::engine::GameplayHost
    {
    public:
        kairo::engine::Entity Tagged{ 77u };
        kairo::engine::Entity NextSpawn{ 100u };
        std::vector<std::string> Messages;
        std::vector<kairo::engine::Entity> Destroyed;
        std::vector<std::pair<kairo::engine::Entity, std::string>> AddedTags;
        std::unordered_map<std::uint32_t, kairo::foundation::math::Vec3d> Positions;

        void Print(kairo::engine::Entity, std::string_view message) override
        { Messages.emplace_back(message); }
        [[nodiscard]] kairo::engine::Entity FindFirstWithTag(std::string_view tag) override
        { return tag == "target" ? Tagged : kairo::engine::Entity{}; }
        [[nodiscard]] bool HasTag(kairo::engine::Entity entity, std::string_view tag) override
        { return entity == Tagged && tag == "target"; }
        [[nodiscard]] kairo::foundation::math::Vec3d GetEntityPosition(
            kairo::engine::Entity entity) override
        { return Positions[entity.Value]; }
        void SetEntityPosition(kairo::engine::Entity entity,
            const kairo::foundation::math::Vec3d& position) override
        { Positions[entity.Value] = position; }
        void SetEntityEnabled(kairo::engine::Entity, bool) override {}
        [[nodiscard]] kairo::engine::Entity SpawnEntity(std::string_view,
            const kairo::foundation::math::Vec3d& position) override
        {
            const kairo::engine::Entity created = NextSpawn;
            ++NextSpawn.Value;
            Positions[created.Value] = position;
            return created;
        }
        void DestroyEntity(kairo::engine::Entity entity) override
        { Destroyed.push_back(entity); }
        void AddTag(kairo::engine::Entity entity, std::string_view tag) override
        { AddedTags.emplace_back(entity, std::string(tag)); }
        void RemoveTag(kairo::engine::Entity, std::string_view) override {}
    };
}

TEST_CASE("Runtime world commits structural gameplay changes atomically")
{
    using namespace kairo::engine;
    Scene authored;
    const Entity root = authored.CreateEntity("Root");
    authored.AddTag(root, "persistent");
    RuntimeWorld world(std::move(authored));

    RuntimeSpawnRequest request;
    request.Name = "Pickup";
    request.Local.Translation = { 2.0f, 3.0f, 4.0f };
    request.Tags = { "collectible", "visible" };
    request.Parent = root;
    const RuntimeSpawnTicket ticket = world.QueueSpawn(std::move(request));
    const RuntimeCommitResult committed = world.Commit();
    REQUIRE(committed.Revision == 1u);
    const Entity pickup = committed.Resolve(ticket).value();
    CHECK(world.Snapshot().Parent(pickup) == root);
    CHECK(world.Snapshot().HasTag(pickup, "collectible"));
    CHECK(world.Snapshot().Transform(pickup).Local.Translation.x == 2.0f);
    CHECK(world.FindByTag("collectible") == std::vector<Entity>{ pickup });

    const std::size_t stableSize = world.Snapshot().Size();
    const std::uint64_t stableRevision = world.Revision();
    world.QueueRename({ 4'000u }, "Missing");
    REQUIRE_THROWS_AS(world.Commit(), std::out_of_range);
    CHECK(world.Snapshot().Size() == stableSize);
    CHECK(world.Revision() == stableRevision);
    CHECK(world.PendingCommandCount() == 1u);
    world.DiscardPending();
}

TEST_CASE("Gameplay VM preserves typed variables across bounded dispatches")
{
    using namespace kairo::engine;
    GameplayProgram program;
    program.RegisterCount = 7u;
    program.Strings = { "target", "Spawned" };
    program.Floats = { 2.0, 1.0 };
    program.Vectors = { { 1.0, 2.0, 3.0 } };
    program.Variables = { { "score", 0.0 } };
    program.Instructions = {
        { GameplayOpcode::LoadFloat, 3u, 0u },
        { GameplayOpcode::SetVariable, 0u, 3u },
        { GameplayOpcode::Halt },
        { GameplayOpcode::GetVariable, 3u, 0u },
        { GameplayOpcode::LoadFloat, 4u, 1u },
        { GameplayOpcode::AddFloat, 3u, 3u, 4u },
        { GameplayOpcode::SetVariable, 0u, 3u },
        { GameplayOpcode::FindFirstWithTag, 5u, 0u },
        { GameplayOpcode::LoadVector3, 6u, 0u },
        { GameplayOpcode::SetEntityPosition, 5u, 6u },
        { GameplayOpcode::Halt },
        { GameplayOpcode::LoadVector3, 3u, 0u },
        { GameplayOpcode::SpawnEntity, 4u, 1u, 3u },
        { GameplayOpcode::AddTag, 4u, 0u },
        { GameplayOpcode::DestroyEntity, 4u },
        { GameplayOpcode::Halt }
    };
    program.Entries = {
        { GameplayEventKind::BeginPlay, {}, 0u },
        { GameplayEventKind::Tick, {}, 3u },
        { GameplayEventKind::InputPressed, "Spawn", 11u }
    };

    const std::vector<std::byte> bytes = SerializeGameplayProgram(program);
    const GameplayProgram restored = ParseGameplayProgram(bytes);
    CHECK(restored.Instructions == program.Instructions);
    CHECK(restored.Variables == program.Variables);

    RecordingGameplayHost host;
    GameplayInstance instance(restored);
    CHECK(instance.Dispatch({ 1u }, { .Event = GameplayEventKind::BeginPlay }, host) == 3u);
    CHECK(std::get<double>(instance.Variable("score")) == 2.0);
    CHECK(instance.Dispatch({ 1u }, { .Event = GameplayEventKind::Tick,
        .DeltaSeconds = 1.0 / 60.0 }, host) == 8u);
    CHECK(std::get<double>(instance.Variable("score")) == 3.0);
    CHECK(host.Positions.at(77u) == kairo::foundation::math::Vec3d{ 1.0, 2.0, 3.0 });

    CHECK(instance.Dispatch({ 1u }, { .Event = GameplayEventKind::InputPressed,
        .Action = "Spawn", .ActionValue = 1.0 }, host) == 5u);
    REQUIRE(host.Destroyed.size() == 1u);
    CHECK(host.Destroyed.front() == Entity{ 100u });
    REQUIRE(host.AddedTags.size() == 1u);
    CHECK(host.AddedTags.front().first == Entity{ 100u });
    CHECK(host.AddedTags.front().second == "target");

    instance.ResetState();
    CHECK(std::get<double>(instance.Variable("score")) == 0.0);

    auto truncated = bytes;
    truncated.pop_back();
    REQUIRE_THROWS_AS(ParseGameplayProgram(truncated), std::invalid_argument);
}

TEST_CASE("Gameplay VM rejects runaway loops and variable type changes")
{
    using namespace kairo::engine;
    RecordingGameplayHost host;
    GameplayProgram loop;
    loop.Instructions = { { GameplayOpcode::Jump, 0u } };
    loop.Entries = { { GameplayEventKind::Tick, {}, 0u } };
    GameplayInstance runaway(loop);
    REQUIRE_THROWS_AS(runaway.Dispatch({ 1u },
        { .Event = GameplayEventKind::Tick, .DeltaSeconds = 0.01 }, host, 32u),
        std::runtime_error);

    GameplayProgram mismatch;
    mismatch.RegisterCount = 4u;
    mismatch.Variables = { { "flag", true } };
    mismatch.Floats = { 1.0 };
    mismatch.Instructions = {
        { GameplayOpcode::LoadFloat, 3u, 0u },
        { GameplayOpcode::SetVariable, 0u, 3u },
        { GameplayOpcode::Halt }
    };
    mismatch.Entries = { { GameplayEventKind::BeginPlay, {}, 0u } };
    GameplayInstance instance(mismatch);
    REQUIRE_THROWS_AS(instance.Dispatch({ 1u },
        { .Event = GameplayEventKind::BeginPlay }, host), std::runtime_error);
}
