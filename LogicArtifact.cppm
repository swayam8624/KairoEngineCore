module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

export module Kairo.EngineCore.LogicArtifact;

import Kairo.Assets;
import Kairo.EngineCore.LogicBytecode;

export namespace kairo::engine
{
    /// Published runtime logic bound to the exact canonical source revision
    /// that produced it. Runtime loaders compare SourceFingerprint with the
    /// current `.kdoc` before admitting Program, preventing stale gameplay
    /// after source-control operations or interrupted multi-file saves.
    struct CompiledLogicArtifact final
    {
        kairo::assets::AssetID Source;
        kairo::assets::AssetFingerprint SourceFingerprint;
        LogicProgram Program;

        void Validate() const
        {
            if (!Source.IsValid())
                throw std::invalid_argument("Compiled logic artifact requires a valid source asset ID.");
            Program.Validate();
        }
    };

    inline constexpr std::size_t MaximumCompiledLogicArtifactBytes =
        64u * 1024u * 1024u + 128u;

    [[nodiscard]] inline std::filesystem::path CompiledLogicPath(
        const std::filesystem::path& projectRoot, kairo::assets::AssetID source)
    {
        if (projectRoot.empty()) throw std::invalid_argument("Compiled logic path requires a project root.");
        if (!source.IsValid()) throw std::invalid_argument("Compiled logic path requires a valid source ID.");
        return projectRoot / ".kairo" / "compiled-logic" / (source.ToString() + ".klogic");
    }

    namespace logic_artifact_detail
    {
        constexpr std::array<std::byte, 8u> Magic{
            std::byte{'K'}, std::byte{'L'}, std::byte{'O'}, std::byte{'G'},
            std::byte{'A'}, std::byte{'R'}, std::byte{'T'}, std::byte{1} };
    }

    [[nodiscard]] inline std::vector<std::byte> SerializeCompiledLogicArtifact(
        const CompiledLogicArtifact& artifact)
    {
        using namespace logic_artifact_detail;
        artifact.Validate();
        const std::vector<std::byte> program = SerializeLogicProgram(artifact.Program);
        kairo::assets::BinaryWriter writer(76u + program.size());
        writer.WriteBytes(Magic);
        for (const std::uint8_t byte : artifact.Source.Bytes()) writer.WriteU8(byte);
        writer.WriteBytes(artifact.SourceFingerprint.Digest);
        writer.WriteU64(artifact.SourceFingerprint.ByteCount);
        writer.WriteU64(static_cast<std::uint64_t>(program.size()));
        writer.WriteBytes(program);
        auto result = std::move(writer).TakeBytes();
        if (result.size() > MaximumCompiledLogicArtifactBytes)
            throw std::length_error("Compiled logic artifact exceeds its 64 MiB safety limit.");
        return result;
    }

    [[nodiscard]] inline CompiledLogicArtifact ParseCompiledLogicArtifact(
        std::span<const std::byte> bytes)
    {
        using namespace logic_artifact_detail;
        if (bytes.size() > MaximumCompiledLogicArtifactBytes)
            throw std::length_error("Compiled logic artifact exceeds its 64 MiB safety limit.");
        kairo::assets::BinaryReader reader(bytes);
        if (!std::ranges::equal(reader.ReadBytes(Magic.size()), Magic))
            throw std::invalid_argument("Compiled logic artifact header is invalid.");
        kairo::assets::AssetID::Storage sourceBytes{};
        for (std::uint8_t& byte : sourceBytes) byte = reader.ReadU8();
        CompiledLogicArtifact result;
        result.Source = kairo::assets::AssetID(sourceBytes);
        const auto digest = reader.ReadBytes(result.SourceFingerprint.Digest.size());
        std::ranges::copy(digest, result.SourceFingerprint.Digest.begin());
        result.SourceFingerprint.ByteCount = reader.ReadU64();
        const std::uint64_t payloadBytes = reader.ReadU64();
        if (payloadBytes != reader.Remaining())
            throw std::invalid_argument("Compiled logic payload length does not match the complete artifact.");
        result.Program = ParseLogicProgram(reader.ReadBytes(static_cast<std::size_t>(payloadBytes)));
        reader.RequireEnd();
        result.Validate();
        return result;
    }

    inline void SaveCompiledLogicArtifact(const std::filesystem::path& path,
        const CompiledLogicArtifact& artifact)
    {
        const std::vector<std::byte> bytes = SerializeCompiledLogicArtifact(artifact);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) throw std::runtime_error("Cannot create compiled logic directory: " + error.message());
        const std::filesystem::path temporary = path.string() +
            ".tmp-" + kairo::assets::GenerateAssetID().ToString();
        try
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("Cannot write compiled logic staging file: " + temporary.string());
            output.close();
            if (!output) throw std::runtime_error("Cannot flush compiled logic staging file: " + temporary.string());
            kairo::assets::ReplaceFileAtomically(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
    }

    [[nodiscard]] inline CompiledLogicArtifact LoadCompiledLogicArtifact(
        const std::filesystem::path& path)
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error) throw std::invalid_argument("Compiled logic artifact is missing: " + path.string());
        if (size > MaximumCompiledLogicArtifactBytes)
            throw std::length_error("Compiled logic artifact exceeds its 64 MiB safety limit.");
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        std::ifstream input(path, std::ios::binary);
        if (!input || (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))))
            throw std::runtime_error("Cannot read compiled logic artifact: " + path.string());
        return ParseCompiledLogicArtifact(bytes);
    }
}
