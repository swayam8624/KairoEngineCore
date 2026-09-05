#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

import Kairo.EngineCore;

TEST_CASE("EngineCore validates and publishes compiler payloads without exposing artifact construction",
    "[KairoEngineCore][Logic][Artifact][PayloadBoundary]")
{
    using namespace kairo::engine;

    const auto source = kairo::assets::AssetID::Parse(
        "00000000-0000-4000-8000-000000000188");
    const std::string sourceText = "kairo-document 1\n";
    const auto sourceBytes = std::as_bytes(std::span(sourceText.data(), sourceText.size()));
    const auto fingerprint = kairo::assets::FingerprintBytes(sourceBytes);

    LogicProgram program;
    program.Strings = { "payload-boundary" };
    program.Instructions = { { LogicOpcode::Print, 0u }, { LogicOpcode::Halt } };
    program.Entries = { { LogicEventKind::BeginPlay, {}, 0u } };
    const std::vector<std::byte> payload = SerializeLogicProgram(program);

    REQUIRE_NOTHROW(ValidateCompiledLogicPayload(payload));

    const auto root = std::filesystem::temp_directory_path() /
        ("kairo-core-payload-boundary-" + kairo::assets::GenerateAssetID().ToString());
    const auto path = CompiledLogicPath(root, source);
    SaveCompiledLogicPayload(path, source, fingerprint, payload);

    const CompiledLogicArtifact restored = LoadCompiledLogicArtifact(path);
    CHECK(restored.Source == source);
    CHECK(restored.SourceFingerprint == fingerprint);
    CHECK(restored.Program.Instructions == program.Instructions);
    CHECK(restored.Program.Entries == program.Entries);

    auto malformed = payload;
    malformed.push_back(std::byte{ 0 });
    REQUIRE_THROWS_AS(ValidateCompiledLogicPayload(malformed), std::invalid_argument);
    REQUIRE_THROWS_AS(SaveCompiledLogicPayload(path, source, fingerprint, malformed),
        std::invalid_argument);

    std::filesystem::remove_all(root);
}
