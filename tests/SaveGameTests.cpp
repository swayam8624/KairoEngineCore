#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

import Kairo.Assets;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.LogicState;
import Kairo.EngineCore.SaveGame;
import Kairo.EngineCore.Scene;
import Kairo.Foundation.Math.Vector;

namespace engine = kairo::engine;
namespace assets = kairo::assets;
using kairo::foundation::math::Vec3d;

TEST_CASE("Save-game archive round trips deterministic subsystem chunks",
    "[EngineCore][SaveGame]")
{
    engine::SaveGameArchive archive;
    archive.ProjectName = "OpenWorldSample";
    archive.EngineVersion = "0.1.0";
    archive.Label = "Mission checkpoint";
    archive.Sequence = 42u;
    archive.SetChunk({ "game.inventory", 3u,
        { std::byte{0x01}, std::byte{0x02}, std::byte{0x7f} } });
    archive.SetChunk({ "game.quests", 1u,
        { std::byte{0x10}, std::byte{0x20} } });

    const auto first = engine::SerializeSaveGame(archive);
    const auto parsed = engine::ParseSaveGame(first);
    const auto second = engine::SerializeSaveGame(parsed);

    CHECK(second == first);
    CHECK(parsed.ProjectName == archive.ProjectName);
    CHECK(parsed.EngineVersion == archive.EngineVersion);
    CHECK(parsed.Label == archive.Label);
    CHECK(parsed.Sequence == 42u);
    CHECK(parsed.ChunkCount() == 2u);
    CHECK(parsed.Chunk("game.inventory").SchemaVersion == 3u);
    CHECK(parsed.Chunk("game.inventory").Payload ==
        archive.Chunk("game.inventory").Payload);
}

TEST_CASE("Save-game chunk CRC rejects corrupted payload bytes",
    "[EngineCore][SaveGame][Corruption]")
{
    engine::SaveGameArchive archive;
    archive.ProjectName = "CorruptionTest";
    archive.EngineVersion = "0.1.0";
    archive.SetChunk({ "game.state", 1u,
        { std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd} } });

    auto bytes = engine::SerializeSaveGame(archive);
    REQUIRE(bytes.size() > 4u);
    bytes.back() ^= std::byte{0x01};
    REQUIRE_THROWS_AS(engine::ParseSaveGame(bytes), std::invalid_argument);
}

TEST_CASE("Scene save chunk restores runtime-authored scene state",
    "[EngineCore][SaveGame][Scene]")
{
    assets::AssetRegistry registry;
    engine::Scene source;
    const engine::Entity parent = source.CreateEntity("Player");
    source.Transform(parent).Local.Translation = { 12.0f, 3.0f, -7.0f };
    source.AddTag(parent, "persistent");
    const engine::Entity child = source.CreateEntity("CameraRig");
    source.SetParent(child, parent);
    source.SetEnabled(child, false);

    const auto chunk = engine::MakeSceneSaveChunk(source, registry);
    const engine::Scene restored = engine::ParseSceneSaveChunk(chunk, registry);

    REQUIRE(restored.Size() == source.Size());
    REQUIRE(restored.Contains(parent));
    REQUIRE(restored.Contains(child));
    CHECK(restored.Name(parent).Value == "Player");
    CHECK(restored.HasTag(parent, "persistent"));
    CHECK(restored.Transform(parent).Local.Translation.x == 12.0f);
    CHECK(restored.Transform(parent).Local.Translation.y == 3.0f);
    CHECK(restored.Transform(parent).Local.Translation.z == -7.0f);
    REQUIRE(restored.Parent(child).has_value());
    CHECK(restored.Parent(child)->Value == parent.Value);
    CHECK_FALSE(restored.IsEnabled(child));
}

TEST_CASE("Logic-state save chunk preserves typed variables and timers",
    "[EngineCore][SaveGame][Logic]")
{
    engine::LogicState source;
    source.Set("wanted", true);
    source.Set("cash", 1250.5);
    source.Set("safehouse", Vec3d{ 10.0, 2.0, -3.0 });
    source.Set("target", engine::Entity{ 17u });
    source.Set("mission", std::string("intro-heist"));
    source.StartTimer("police-cooldown", 8.5, 12.0);

    const auto chunk = engine::MakeLogicStateSaveChunk(source);
    engine::LogicState restored = engine::ParseLogicStateSaveChunk(chunk);

    CHECK(restored.Snapshot() == source.Snapshot());
    CHECK(restored.Get<bool>("wanted"));
    CHECK(restored.Get<double>("cash") == 1250.5);
    CHECK(restored.Get<Vec3d>("safehouse").z == -3.0);
    CHECK(restored.Get<engine::Entity>("target").Value == 17u);
    CHECK(restored.Get<std::string>("mission") == "intro-heist");
    CHECK(restored.HasTimer("police-cooldown"));
}

TEST_CASE("Save-game archive enforces portable chunk names and schemas",
    "[EngineCore][SaveGame][Validation]")
{
    engine::SaveGameArchive archive;
    archive.ProjectName = "Validation";
    archive.EngineVersion = "0.1.0";
    REQUIRE_THROWS_AS(archive.SetChunk({ "bad chunk", 1u, {} }),
        std::invalid_argument);
    REQUIRE_THROWS_AS(archive.SetChunk({ "game.state", 0u, {} }),
        std::invalid_argument);

    archive.SetChunk({ "game.state", 1u, {} });
    CHECK(archive.ContainsChunk("game.state"));
    CHECK(archive.RemoveChunk("game.state"));
    CHECK_FALSE(archive.ContainsChunk("game.state"));
}
